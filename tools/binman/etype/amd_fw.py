# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# Entry-type module for the firmware which an AMD PSP reads from the flash
#

import struct
import zlib

from binman.entry import Entry
from binman.etype.section import Entry_section
from binman import state
from dtoc import fdt_util
from u_boot_pylib import tools

# Signature at the start of the Embedded Firmware Structure (EFS)
EFS_SIGNATURE = 0x55aa55aa

# Size of the EFS
EFS_SIZE = 0x60

# Value of the EFS 'multi-generation' word, by SoC
SOCS = {
    'genoa': 0xfffffffe,
    'turin': 0xffffffe3,
}

# Directory cookies for the first and second levels
PSP_COOKIE = b'$PSP'
PSP_L2_COOKIE = b'$PL2'
BIOS_COOKIE = b'$BHD'
BIOS_L2_COOKIE = b'$BL2'

DIR_HDR_SIZE = 0x10
PSP_ENTRY_SIZE = 0x10
BIOS_ENTRY_SIZE = 0x18
MAX_PSP_ENTRIES = 0xff
MAX_BIOS_ENTRIES = 0x2f

# Alignments, as used by coreboot's amdfwtool
BODY_ALIGN = 0x10000
TABLE_ALIGN = 0x1000
ERASE_ALIGN = 0x1000
BLOB_ALIGN = 0x100

# Address mode for directories and entries: an offset into the flash
ADDR_MODE_FLASH = 1

# Mask for the address within a directory entry, below the address mode
ADDR_MASK = (1 << 62) - 1

# Entry types which need special handling
PSP_TYPE_L2_PTR = 0x40
BIOS_TYPE_APCB = 0x60
BIOS_TYPE_BIN = 0x62
BIOS_TYPE_APCB_BACKUP = 0x68
BIOS_TYPE_L2_PTR = 0x70

# Order of BIOS-directory types, as used by amdfwtool; any others follow
BIOS_ORDER = (0x05, 0x07, 0x68, 0x60, 0x61, 0x62, 0x63, 0x6d, 0x64, 0x65,
              0x66, 0x69, 0x6a, 0x6b)

# Header in front of a compressed BIOS image, holding the compressed size
COMP_HDR_SIZE = 0x100
COMP_SIZE_OFFSET = 0x14


def fletcher32(data):
    """Calculate the checksum used by PSP and BIOS directories

    Args:
        data (bytes): Data to checksum, an even number of bytes

    Returns:
        int: 32-bit checksum
    """
    words = struct.unpack(f'<{len(data) // 2}H', data)
    c0 = 0xffff
    c1 = 0xffff

    # 359 words is the most which can be summed without overflowing 32 bits
    for start in range(0, len(words), 359):
        for word in words[start:start + 359]:
            c0 += word
            c1 += c0
        c0 = (c0 & 0xffff) + (c0 >> 16)
        c1 = (c1 & 0xffff) + (c1 >> 16)
    c0 = (c0 & 0xffff) + (c0 >> 16)
    c1 = (c1 & 0xffff) + (c1 >> 16)
    return c1 << 16 | c0

def compress_bios(data):
    """Compress a BIOS image as the PSP expects it

    The image is compressed with zlib and given a header which is empty apart
    from the compressed size. This matches coreboot's amdcompress tool.

    Args:
        data (bytes): Image to compress

    Returns:
        bytes: Header followed by the compressed image
    """
    comp = zlib.compress(data)
    hdr = bytearray(COMP_HDR_SIZE)
    struct.pack_into('<I', hdr, COMP_SIZE_OFFSET, len(comp))
    return bytes(hdr) + comp

def align(value, size):
    """Align a value up to a multiple of a power-of-two size"""
    return (value + size - 1) & ~(size - 1)

def sort_key(item):
    """Get the key to sort directory entries into amdfwtool's order

    PSP entries are in type order and BIOS entries in the order given by
    BIOS_ORDER, each then by sub-program and instance

    Args:
        item (AmdFwItem): Directory entry

    Returns:
        tuple: Key to sort by
    """
    if item.bios:
        rank = (BIOS_ORDER.index(item.dtype) if item.dtype in BIOS_ORDER
                else len(BIOS_ORDER))
        return rank, item.dtype, item.subprog, item.inst
    return 0, item.dtype, item.subprog, item.inst


class AmdFwItem:
    """A directory entry, as described by a subnode of the amd-fw node

    Properties:
        name (str): Name of the subnode
        entry (Entry): Entry providing the contents, or None if the directory
            entry has no contents in this section
        bios (bool): True for a BIOS-directory entry, False for PSP
        dtype (int): Directory-entry type
        subprog (int): Sub-program
        inst (int): Instance
        levels (set of int): Directory levels to put the entry in (1 and/or 2)
        value (int): Value to store in the entry instead of an address, or
            None
        region (tuple): Offset and size of a region of the flash which the
            entry describes, or None
        reserve (int): Number of bytes to reserve in the section, filled with
            0xff, or None
        writable (bool): True if the PSP may write to the entry's region
        region_type (int): Memory-region type (BIOS directory only)
        reset (bool): True if this is the reset image (BIOS directory only)
        copy (bool): True to copy the entry to its destination (BIOS
            directory only)
        read_only (bool): True to make the destination read-only (BIOS
            directory only)
        compress (bool): True to compress the entry's contents (BIOS directory
            only)
        dest (int): Destination address (BIOS directory only)
        data (bytes): Contents to place in the section, once obtained
        uncomp_size (int): Size of the contents before compression
        pos (int): Offset of the contents within the section, once laid out
    """
    def __init__(self, name):
        self.name = name
        self.entry = None
        self.bios = False
        self.dtype = None
        self.subprog = 0
        self.inst = 0
        self.levels = {1, 2}
        self.value = None
        self.region = None
        self.reserve = None
        self.writable = False
        self.region_type = 0
        self.reset = False
        self.copy = False
        self.read_only = False
        self.compress = False
        self.dest = None
        self.data = None
        self.uncomp_size = None
        self.pos = None


class Entry_amd_fw(Entry_section):
    """Firmware which an AMD Platform Security Processor reads from the flash

    Properties / Entry arguments:
        - amd,soc: SoC the image is for: "genoa" or "turin"
        - amd,spi-read-mode: SPI read mode for the PSP to use (see amdfwtool's
          --spi-read-mode)
        - amd,spi-speed: SPI speed for the PSP to use (see amdfwtool's
          --spi-speed)
        - amd,spi-micron-flag: 1 if Micron flash parts are used, else 0
          (default 0)
        - amd,espi-config: Four bytes of eSPI configuration: espi0-config,
          espi1-config, espi0-config1 and espi1-config1 (default 0xff each)

    On AMD SoCs the Platform Security Processor (PSP) starts before the x86
    cores, loads its own firmware and AMD's boot loader (the ABL) from the
    flash, trains the memory and then loads the BIOS image into DRAM and
    releases the boot core. It finds all of this through an Embedded Firmware
    Structure (EFS), which this entry starts with, and two sets of
    directories: one for the PSP and one for the 'BIOS', i.e. for the x86 side.
    Each has two levels, the second pointed to from the first.

    This entry builds the same layout as coreboot's amdfwtool with its
    --multilevel option, so the entry must be at a 64KB-aligned position in
    an image which starts at the start of the flash, since the directories
    hold offsets into the flash. The PSP looks for the EFS at a few fixed
    positions, such as 0x20000.

    Each subnode is an entry in one of the directories. The contents of a
    subnode are provided in the usual way, e.g. by a blob-ext entry or a
    section. These properties describe the directory entry:

        - amd,psp-type / amd,bios-type: Type of the entry, which also selects
          the directory it goes in
        - amd,subprog: Sub-program (default 0)
        - amd,instance: Instance (default 0)
        - amd,level: Directory level: "1", "2" or "both" (default "both")
        - amd,writable: Allow the PSP to write to the entry's region

    BIOS-directory entries can also have:

        - amd,dest: 64-bit destination address
        - amd,region-type: Memory-region type (default 0)
        - amd,reset: The entry is the reset image
        - amd,copy: Copy the entry to its destination
        - amd,read-only: Make the destination read-only
        - amd,compress: Compress the contents, as the PSP expects for the BIOS
          image; the entry's size is then the size before compression

    Some entries have no contents in this section. These use properties
    instead of an entry type:

        - amd,value: 64-bit value to store in place of an address, with a size
          of 0xffffffff, e.g. for the soft-fuse chain
        - amd,region: Offset and size of a region of the flash, outside this
          section, which the entry describes, e.g. for non-volatile storage
        - amd,reserve: Number of bytes to reserve, filled with 0xff and
          aligned to 4KB, e.g. for the token-unlock area

    The entries are sorted into the order which amdfwtool uses, so the order
    of the subnodes does not matter: PSP entries are in type order and BIOS
    entries in amdfwtool's order (APCB, APOB, BIOS image, APOB NV,
    memory-training firmware, microcode and so on), each then by sub-program
    and instance. This means that SoC and board parts can be given
    separately, e.g. with templates.

    An entry in both levels is copied into each, except for the BIOS image
    (type 0x62), which the second level shares with the first. Entries of
    type 0x60 and 0x68 (APCB) are aligned to 4KB and other contents to 256
    bytes. The pointers from the first level to the second are added
    automatically.

    For example::

        amd-fw {
            offset = <0x20000>;
            amd,soc = "turin";
            amd,spi-read-mode = <0>;
            amd,spi-speed = <0>;
            amd,espi-config = <0x0e 0xff 0xff 0xff>;

            amd-pubkey {
                type = "blob-ext";
                filename = "TypeId0x00_AmdPubKey_BRH.tkn";
                amd,psp-type = <0x00>;
                amd,level = "1";
            };
            soft-fuse {
                amd,psp-type = <0x0b>;
                amd,value = /bits/ 64 <1>;
            };
            sev-nvram {
                amd,psp-type = <0x38>;
                amd,region = <0xe48000 0x8000>;
                amd,writable;
            };
            bios {
                type = "section";
                amd,bios-type = <0x62>;
                amd,dest = /bits/ 64 <0x7150000>;
                amd,reset;
                amd,copy;
                amd,compress;

                u-boot-spl {
                };
            };
        };

    AMD's firmware is not part of U-Boot. AMD publishes it for use with
    coreboot, e.g. in coreboot's amd_blobs repository or with Dasharo's
    releases, and the configuration (APCB) for a board comes with its
    coreboot port.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node)
        self._items = []
        self._entry_items = {}
        self._soc = None
        self._spi_read_mode = None
        self._spi_speed = None
        self._micron = None
        self._espi = None
        self._psp_entries = None
        self._psp_l2_index = None

    def ReadNode(self):
        super().ReadNode()
        self._soc = fdt_util.GetString(self._node, 'amd,soc')
        if self._soc not in SOCS:
            self.Raise(f"Unknown SoC '{self._soc}' (use {', '.join(SOCS)})")
        self._spi_read_mode = fdt_util.GetInt(self._node, 'amd,spi-read-mode')
        self._spi_speed = fdt_util.GetInt(self._node, 'amd,spi-speed')
        if self._spi_read_mode is None or self._spi_speed is None:
            self.Raise("Missing 'amd,spi-read-mode' or 'amd,spi-speed'")
        self._micron = fdt_util.GetInt(self._node, 'amd,spi-micron-flag', 0)
        if self._micron not in (0, 1):
            self.Raise(f"Invalid 'amd,spi-micron-flag' {self._micron}")
        self._espi = self._get_ints(self._node, 'amd,espi-config',
                                    [0xff] * 4)
        if len(self._espi) != 4:
            self.Raise("'amd,espi-config' must have four values")

    @staticmethod
    def _get_ints(node, propname, default=None):
        """Read a property holding a list of 32-bit integers"""
        prop = node.props.get(propname)
        if not prop:
            return default
        value = prop.value if isinstance(prop.value, list) else [prop.value]
        return [fdt_util.fdt32_to_cpu(val) for val in value]

    def _read_item(self, node):
        """Read a directory entry from a subnode

        Args:
            node (Node): Subnode to read

        Returns:
            AmdFwItem: Directory entry
        """
        item = AmdFwItem(node.name)
        psp_type = fdt_util.GetInt(node, 'amd,psp-type')
        bios_type = fdt_util.GetInt(node, 'amd,bios-type')
        if (psp_type is None) == (bios_type is None):
            self.Raise(f"Subnode '{node.name}': Need one of 'amd,psp-type' "
                       "and 'amd,bios-type'")
        item.bios = bios_type is not None
        item.dtype = bios_type if item.bios else psp_type
        item.subprog = fdt_util.GetInt(node, 'amd,subprog', 0)
        item.inst = fdt_util.GetInt(node, 'amd,instance', 0)
        level = fdt_util.GetString(node, 'amd,level', 'both')
        levels = {'1': {1}, '2': {2}, 'both': {1, 2}}.get(level)
        if not levels:
            self.Raise(f"Subnode '{node.name}': Invalid 'amd,level' '{level}' "
                       "(use 1, 2 or both)")
        item.levels = levels
        item.value = fdt_util.GetInt64(node, 'amd,value')
        item.region = self._get_ints(node, 'amd,region')
        if item.region:
            if len(item.region) != 2:
                self.Raise(f"Subnode '{node.name}': 'amd,region' must have "
                           'an offset and a size')
            if item.region[0] % ERASE_ALIGN:
                self.Raise(f"Subnode '{node.name}': Region offset "
                           f'{item.region[0]:#x} is not 4KB-aligned')
        item.reserve = fdt_util.GetInt(node, 'amd,reserve')
        item.writable = fdt_util.GetBool(node, 'amd,writable')
        item.region_type = fdt_util.GetInt(node, 'amd,region-type', 0)
        item.reset = fdt_util.GetBool(node, 'amd,reset')
        item.copy = fdt_util.GetBool(node, 'amd,copy')
        item.read_only = fdt_util.GetBool(node, 'amd,read-only')
        item.compress = fdt_util.GetBool(node, 'amd,compress')
        item.dest = fdt_util.GetInt64(node, 'amd,dest')
        return item

    def ReadEntries(self):
        for node in self._node.subnodes:
            item = self._read_item(node)
            if (item.value is None and item.region is None and
                    item.reserve is None):
                entry = Entry.Create(self, node,
                                     expanded=self.GetImage().use_expanded,
                                     missing_etype=self.GetImage().missing_etype)
                entry.ReadNode()
                entry.SetPrefix(self._name_prefix)
                self._entries[node.name] = entry
                item.entry = entry
                self._entry_items[node.name] = item
            self._items.append(item)

    def _pack_psp_entry(self, item, size, addr, mode=ADDR_MODE_FLASH):
        """Pack a PSP-directory entry"""
        flags = int(item.writable) << 2 | (item.inst & 0xf) << 3
        return struct.pack('<BBHIQ', item.dtype, item.subprog, flags,
                           size & 0xffffffff, addr & ADDR_MASK | mode << 62)

    def _pack_bios_entry(self, item, size, addr):
        """Pack a BIOS-directory entry"""
        flags = (int(item.reset) | int(item.copy) << 1 |
                 int(item.read_only) << 2 | int(item.compress) << 3 |
                 (item.inst & 0xf) << 4)
        flags2 = (item.subprog & 7) | int(item.writable) << 5
        dest = (1 << 64) - 1 if item.dest is None else item.dest
        return struct.pack('<BBBBIQQ', item.dtype, item.region_type, flags,
                           flags2, size, addr & ADDR_MASK |
                           ADDR_MODE_FLASH << 62, dest)

    @staticmethod
    def _write_header(buf, dir_pos, cookie, entries, dir_size=None):
        """Write a directory header and its entries

        Args:
            buf (bytearray): Buffer holding the section contents
            dir_pos (int): Offset of the directory in the buffer
            cookie (bytes): Directory cookie
            entries (list of bytes): Packed entries
            dir_size (int): Size of the directory and its contents, or None to
                keep the existing value
        """
        body = b''.join(entries)
        start = dir_pos + DIR_HDR_SIZE
        buf[start:start + len(body)] = body
        if dir_size is None:
            info, = struct.unpack_from('<I', buf, dir_pos + 12)
        else:
            info = (dir_size // TABLE_ALIGN | 1 << 10 |
                    ADDR_MODE_FLASH << 29)
        struct.pack_into('<4sII', buf, dir_pos, cookie, 0, len(entries))
        struct.pack_into('<I', buf, dir_pos + 12, info)
        csum = fletcher32(bytes(buf[dir_pos + 8:start + len(body)]))
        struct.pack_into('<I', buf, dir_pos + 4, csum)

    @staticmethod
    def _add_data(buf, pos, data):
        """Copy data into the buffer

        Args:
            buf (bytearray): Buffer to write into
            pos (int): Offset to write at
            data (bytes): Data to write

        Returns:
            int: Next free offset, aligned for the next contents
        """
        buf[pos:pos + len(data)] = data
        return align(pos + len(data), BLOB_ALIGN)

    def _layout_psp(self, buf, pos, base, level):
        """Lay out one level of the PSP directory

        The first level has a place for the pointer to the second, which is
        filled in once the second level is laid out

        Args:
            buf (bytearray): Buffer to write into
            pos (int): Next free offset in the buffer
            base (int): Offset of this section in the flash
            level (int): Directory level (1 or 2)

        Returns:
            tuple:
                int: Offset of the directory in the buffer
                int: Next free offset in the buffer
        """
        dir_pos = align(pos, TABLE_ALIGN)
        pos = align(dir_pos + DIR_HDR_SIZE + MAX_PSP_ENTRIES * PSP_ENTRY_SIZE,
                    TABLE_ALIGN)
        items = [item for item in self._items
                 if not item.bios and level in item.levels]
        ptr = AmdFwItem('psp-l2-ptr')
        ptr.dtype = PSP_TYPE_L2_PTR
        if level == 1:
            items.append(ptr)
        entries = []
        for item in sorted(items, key=sort_key):
            if item is ptr:
                self._psp_l2_index = len(entries)
                entries.append(None)
            elif item.value is not None:
                entries.append(self._pack_psp_entry(item, 0xffffffff,
                                                    item.value,
                                                    item.value >> 62))
            elif item.region is not None:
                entries.append(self._pack_psp_entry(item, item.region[1],
                                                    item.region[0]))
            elif item.reserve is not None:
                pos = align(pos, ERASE_ALIGN)
                entries.append(self._pack_psp_entry(item, item.reserve,
                                                    base + pos))
                pos = self._add_data(buf, pos, tools.get_bytes(0xff,
                                                               item.reserve))
            else:
                if item.pos is None:
                    item.pos = pos
                entries.append(self._pack_psp_entry(item, len(item.data),
                                                    base + pos))
                pos = self._add_data(buf, pos, item.data)
        pos = align(pos, TABLE_ALIGN)
        if level == 1:
            self._psp_entries = entries
            entries = [entry for entry in entries if entry]
        self._write_header(buf, dir_pos, PSP_COOKIE if level == 1 else
                           PSP_L2_COOKIE, entries, pos - dir_pos)
        return dir_pos, pos

    def _layout_bios(self, buf, pos, base, level):
        """Lay out one level of the BIOS directory

        Args:
            buf (bytearray): Buffer to write into
            pos (int): Next free offset in the buffer
            base (int): Offset of this section in the flash
            level (int): Directory level (1 or 2)

        Returns:
            tuple:
                int: Offset of the directory in the buffer
                int: Next free offset in the buffer
                list of bytes: Packed entries
        """
        dir_pos = align(pos, TABLE_ALIGN)
        pos = align(dir_pos + DIR_HDR_SIZE +
                    MAX_BIOS_ENTRIES * BIOS_ENTRY_SIZE, TABLE_ALIGN)
        entries = []
        items = [item for item in self._items
                 if item.bios and level in item.levels]
        for item in sorted(items, key=sort_key):
            if item.region is not None:
                entries.append(self._pack_bios_entry(item, item.region[1],
                                                     item.region[0]))
            elif item.reserve is not None or item.value is not None:
                self.Raise(f"Subnode '{item.name}': BIOS-directory entries "
                           "cannot use 'amd,reserve' or 'amd,value'")
            elif item.dtype == BIOS_TYPE_BIN and item.pos is not None:
                # The second level shares the first level's copy
                entries.append(self._pack_bios_entry(
                    item, item.uncomp_size, base + item.pos))
            else:
                if item.dtype in (BIOS_TYPE_APCB, BIOS_TYPE_APCB_BACKUP):
                    pos = align(pos, ERASE_ALIGN)
                if item.pos is None:
                    item.pos = pos
                entries.append(self._pack_bios_entry(item, item.uncomp_size,
                                                     base + pos))
                pos = self._add_data(buf, pos, item.data)
        pos = align(pos, TABLE_ALIGN)
        self._write_header(buf, dir_pos, BIOS_COOKIE if level == 1 else
                           BIOS_L2_COOKIE, entries, pos - dir_pos)
        return dir_pos, pos, entries

    def _build_efs(self, buf, psp_addr, bios_addr):
        """Write the Embedded Firmware Structure at the start of the buffer"""
        struct.pack_into('<IIII', buf, 0, EFS_SIGNATURE, 0, 0, 0)
        struct.pack_into('<IIII', buf, 0x14, psp_addr, 0, 0, 0)
        struct.pack_into('<IIIII', buf, 0x24, SOCS[self._soc], bios_addr, 0,
                         0, 0)
        struct.pack_into('<BB', buf, 0x40, self._spi_read_mode,
                         self._spi_speed)
        struct.pack_into('<B', buf, 0x43, self._spi_read_mode)
        struct.pack_into('<B', buf, 0x45, 0xa if self._micron else 0xff)
        struct.pack_into('<HH4BI', buf, 0x4c, 0, 0, *self._espi, 0)

    def _layout(self, base):
        """Lay out the EFS, directories and contents

        Args:
            base (int): Offset of this section in the flash

        Returns:
            bytes: Contents of the section
        """
        self._psp_entries = None
        for item in self._items:
            item.pos = None
        # Allow for the EFS, the four directories with their alignment and
        # the contents of every entry in both levels, each aligned
        size = BODY_ALIGN + 4 * 2 * TABLE_ALIGN
        for item in self._items:
            size += 2 * (len(item.data or b'') + (item.reserve or 0) +
                         2 * ERASE_ALIGN)
        buf = bytearray(tools.get_bytes(0xff, size))

        pos = align(EFS_SIZE, BODY_ALIGN)
        psp_pos, pos = self._layout_psp(buf, pos, base, 1)
        psp_l2_pos, pos = self._layout_psp(buf, pos, base, 2)

        # Point the first level to the second
        l2_count, = struct.unpack_from('<I', buf, psp_l2_pos + 8)
        ptr = AmdFwItem('psp-l2-ptr')
        ptr.dtype = PSP_TYPE_L2_PTR
        self._psp_entries[self._psp_l2_index] = self._pack_psp_entry(
            ptr, DIR_HDR_SIZE + l2_count * PSP_ENTRY_SIZE, base + psp_l2_pos)
        self._write_header(buf, psp_pos, PSP_COOKIE, self._psp_entries)

        bios_pos, pos, bios_entries = self._layout_bios(buf, pos, base, 1)
        bios_l2_pos, pos, _ = self._layout_bios(buf, pos, base, 2)
        ptr = AmdFwItem('bios-l2-ptr')
        ptr.bios = True
        ptr.dtype = BIOS_TYPE_L2_PTR
        bios_entries.append(self._pack_bios_entry(
            ptr, MAX_BIOS_ENTRIES * BIOS_ENTRY_SIZE, base + bios_l2_pos))
        self._write_header(buf, bios_pos, BIOS_COOKIE, bios_entries)

        self._build_efs(buf, base + psp_pos, base + bios_pos)
        return bytes(buf[:pos])

    def BuildSectionData(self, required):
        for item in self._entry_items.values():
            data = item.entry.GetData(required)
            if data is None:
                return None
            item.uncomp_size = len(data)
            if item.compress:
                data = compress_bios(data)
            item.data = data
        return self._layout(self._flash_offset())

    def _flash_offset(self):
        """Get the offset of this section in the flash

        This is its position in the image, excluding any skip-at-start, since
        the image starts at the start of the flash

        Returns:
            int: Offset, or 0 if not yet known
        """
        if self.image_pos is None:
            return 0
        return self.image_pos - self.GetImage().GetStartOffset()

    def CheckEntries(self):
        # The entries are laid out by this section, not packed in order, so
        # only check the entries themselves
        for entry in self._entries.values():
            entry.CheckEntries()

    def AddMissingProperties(self, have_image_pos):
        Entry.AddMissingProperties(self, have_image_pos)
        for item in self._entry_items.values():
            entry = item.entry
            if item.compress:
                # The contents of a compressed entry have no position in the
                # image
                Entry.AddMissingProperties(entry, have_image_pos)
                for subentry in (entry.GetEntries() or {}).values():
                    subentry.AddMissingProperties(False)
                state.AddZeroProp(entry._node, 'uncomp-size')
            else:
                entry.AddMissingProperties(have_image_pos)

    def SetImagePos(self, image_pos):
        Entry.SetImagePos(self, image_pos)
        for item in self._entry_items.values():
            entry = item.entry
            entry.offset = item.pos
            entry.size = len(item.data)
            if item.compress:
                # The contents of a compressed entry have no position in the
                # image, so leave them alone
                Entry.SetImagePos(entry, image_pos + self.offset)
                entry.uncomp_size = item.uncomp_size
            else:
                entry.SetImagePos(image_pos + self.offset)

    def ProcessContents(self):
        sizes_ok = super().ProcessContents()
        offset = self._flash_offset()
        if offset % BODY_ALIGN:
            self.Raise(f'Offset {offset:#x} in the flash must be '
                       '64KB-aligned')
        data = self.BuildSectionData(True)
        return self.ProcessContentsUpdate(data) and sizes_ok
