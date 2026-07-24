# SPDX-License-Identifier: GPL-2.0+
# Copyright (c) 2016 Google, Inc
# Written by Simon Glass <sjg@chromium.org>
#
# Entry-type module for Intel Firmware Interface Table
#

import struct

from binman.etype.blob_ext import Entry_blob_ext
from dtoc import fdt_util

class Entry_intel_fit(Entry_blob_ext):
    """Intel Firmware Interface Table (FIT)

    This entry contains an Intel FIT as required by recent Intel CPUs. The
    Intel FIT contains information about the firmware and microcode available
    in the image, which the CPU consumes at reset, before fetching the reset
    vector. An intel-fit-ptr entry at 0xffffffc0 points to it.

    Properties / Entry arguments:
        - fit,microcode: Name of the sibling entry containing microcode
            update(s). One type-1 Intel FIT entry is created for each update
            in it. If missing, the Intel FIT contains only its header entry.

    The microcode sibling holds one or more standard Intel microcode
    updates, concatenated; the total-size field of each update's header is
    used to find the next one. On CPUs which require it (such as Alder
    Lake), the microcode ROM applies the update indicated by the Intel FIT
    before the CPU executes the reset vector, so the entry must be present
    for the CPU to boot properly.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node)
        self._microcode_name = None

    def ReadNode(self):
        """Force 64-byte alignment as required by the FIT specification"""
        super().ReadNode()
        if not self.align or self.align < 64:
            self.align = 64
        self._microcode_name = fdt_util.GetString(self._node,
                                                  'fit,microcode')
        if self._microcode_name and not self.HasSibling(self._microcode_name):
            self.Raise(f"No sibling '{self._microcode_name}' for microcode")

    def _GetUcodeOffsets(self):
        """Find each microcode update in the microcode sibling

        Walks the sibling entry's contents using the total-size field of
        each update's header.

        Returns:
            tuple:
                int: Image position of the sibling (0 if not yet known)
                list of int: Offset of each microcode update within it
        """
        entry = self.section.GetEntries()[self._microcode_name]
        data = entry.GetData(required=False)
        offsets = []
        if data:
            pos = 0
            while pos + 0x30 <= len(data):
                # Stop at anything which is not a microcode update, e.g.
                # zero padding on the end of the file (the header-version
                # field of an update is always 1)
                if struct.unpack_from('<L', data, pos)[0] != 1:
                    break
                total_size = struct.unpack_from('<L', data, pos + 0x20)[0]
                if not total_size:
                    total_size = 2048
                offsets.append(pos)
                pos += total_size
        return entry.image_pos or 0, offsets

    def _GetContents(self):
        ucode = []
        if self._microcode_name:
            pos, offsets = self._GetUcodeOffsets()
            ucode = [pos + off for off in offsets]

        count = 1 + len(ucode)

        # Header entry: the address field holds the '_FIT_   ' signature and
        # the size field the number of entries. The checksum-valid bit (0x80)
        # is set, with the checksum byte calculated below
        data = struct.pack('<8sIHBB', b'_FIT_   ', count, 0x100, 0x80, 0)

        # One type-1 entry per microcode update; their size, version and
        # checksum fields are not used
        for addr in ucode:
            data += struct.pack('<QIHBB', addr, 0, 0x100, 1, 0)

        # Set the header checksum so that the table sums to zero
        data = bytearray(data)
        data[15] = (0x100 - sum(data)) & 0xff

        return bytes(data)

    def ObtainContents(self):
        self.SetContents(self._GetContents())
        return True

    def ProcessContents(self):
        """Write an updated version of the Intel FIT to this entry

        This is necessary since the microcode's image_pos is not available
        when ObtainContents() is called, since by then the entries have not
        been packed in the image.
        """
        return self.ProcessContentsUpdate(self._GetContents())
