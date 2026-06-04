# SPDX-License-Identifier: GPL-2.0+
# Copyright 2025 Google LLC
#
# Entry-type module for the Intel OS Image Profile (OSIP) header

"""Entry-type module for an Intel OS Image Profile (OSIP) header

This generates the 512-byte '$OS$' header used by the boot ROM on Intel Atom
SoCs (Medfield / Clovertrail / Merrifield / Bay Trail) to locate and load the
OS image. On the Intel Edison (Merrifield) this OS image is U-Boot.

The OS image is expected to immediately follow this header, i.e. at offset
0x200 in the containing section. A single OS Image Identifier (OSII) descriptor
is emitted, pointing at that image.
"""

import struct

from binman.entry import Entry
from dtoc import fdt_util

OSIP_SIGNATURE = b'$OS$'
OSIP_SIZE = 0x200           # The header occupies one 512-byte block
HEADER_SIZE = 0x38          # Size of the parsed OSIP header (sig + one OSII)
OSII_OFFSET = 0x20          # First OS Image Identifier descriptor
MBR_OFFSET = 0x1b8          # Start of the protective MBR (disk sig + table)
MBR_PART_OFFSET = 0x1be     # First MBR partition entry
BOOT_SIG_OFFSET = 0x1fe     # Legacy 0x55aa boot signature
GPT_PROTECTIVE_TYPE = 0xee  # MBR partition type for a GPT-protective entry

# Sector count placed in the protective MBR entry, matching the reference
# Edison images. The on-disk GPT is authoritative, so this is informational.
PROTECTIVE_MBR_SECTORS = 0x03a3e000

class Entry_intel_osip(Entry):
    """Intel OS Image Profile (OSIP) header

    Properties / Entry arguments:
        - intel,load-address (int): DDR address to load the OS image to
          (default 0x01100000)
        - intel,entry-point (int): Execution entry point of the OS image
          (default 0x01101000)
        - intel,lba (int): Logical block address of the image on the boot
          media (default 0x800)
        - intel,size-blocks (int): Size of the OS image in 512-byte blocks
          (default 0x3000)
        - intel,attribute (int): OSII attribute byte (default 0x0f)

    The header checksum is calculated automatically (an XOR over the header
    bytes). A GPT protective MBR and the legacy 0x55aa boot signature are
    placed in the final bytes of the block, as the boot ROM requires a valid
    protective MBR in this sector before it will load the OS image.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node)
        self.load = fdt_util.GetInt(self._node, 'intel,load-address',
                                    0x01100000)
        self.entry = fdt_util.GetInt(self._node, 'intel,entry-point',
                                     0x01101000)
        self.lba = fdt_util.GetInt(self._node, 'intel,lba', 0x800)
        self.size_blocks = fdt_util.GetInt(self._node, 'intel,size-blocks',
                                           0x3000)
        self.attribute = fdt_util.GetInt(self._node, 'intel,attribute', 0x0f)

    def _build_header(self):
        data = bytearray(b'\xff' * OSIP_SIZE)

        # OSIP header: signature, revisions, checksum, pointer/image counts and
        # the header size. The checksum byte is filled in last.
        struct.pack_into('<4sBBBBBBH', data, 0,
                         OSIP_SIGNATURE,    # 0x00 '$OS$'
                         0,                 # 0x04 reserved
                         0,                 # 0x05 header minor revision
                         1,                 # 0x06 header major revision
                         0,                 # 0x07 header checksum (see below)
                         1,                 # 0x08 number of pointers
                         1,                 # 0x09 number of images
                         HEADER_SIZE)       # 0x0a header size
        # Reserved area between the header and the first OSII descriptor
        data[0x0c:OSII_OFFSET] = bytes(OSII_OFFSET - 0x0c)

        # OS Image Identifier (OSII) descriptor describing the OS image
        struct.pack_into('<HHIIIIB', data, OSII_OFFSET,
                         0,                 # 0x20 OS minor revision
                         0,                 # 0x22 OS major revision
                         self.lba,          # 0x24 logical start block
                         self.load,         # 0x28 DDR load address
                         self.entry,        # 0x2c entry point
                         self.size_blocks,  # 0x30 size in 512-byte blocks
                         self.attribute)    # 0x34 attribute
        data[0x35:HEADER_SIZE] = bytes(HEADER_SIZE - 0x35)  # OSII reserved

        # GPT protective MBR. The Edison's x86 mask ROM only boots U-Boot from
        # an eMMC whose first sector is a valid protective MBR; this OSIP header
        # doubles as that sector. Without the 0xee entry the ROM drops to DnX
        # instead of loading U-Boot. The entry covers the disk from LBA 1.
        data[MBR_OFFSET:BOOT_SIG_OFFSET] = bytes(BOOT_SIG_OFFSET - MBR_OFFSET)
        struct.pack_into('<B3sB3sII', data, MBR_PART_OFFSET,
                         0,                       # boot indicator (inactive)
                         b'\0\0\0',               # start CHS (unused)
                         GPT_PROTECTIVE_TYPE,     # partition type 0xee
                         b'\0\0\0',               # end CHS (unused)
                         1,                       # first LBA (the GPT header)
                         PROTECTIVE_MBR_SECTORS)  # number of sectors

        # Legacy boot signature at the end of the block
        data[BOOT_SIG_OFFSET] = 0x55
        data[BOOT_SIG_OFFSET + 1] = 0xaa

        # Header checksum: XOR over the header bytes (checksum byte counted
        # as zero, since it is still zero at this point)
        checksum = 0
        for byte in data[:HEADER_SIZE]:
            checksum ^= byte
        data[7] = checksum

        return bytes(data)

    def ObtainContents(self):
        self.SetContents(self._build_header())
        return True
