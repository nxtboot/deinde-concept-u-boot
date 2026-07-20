# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# Entry-type module for a coreboot ROM image
#

from binman import elf
from binman.entry import Entry, EntryArg
from binman.etype.section import Entry_section
from dtoc import fdt_util
from u_boot_pylib import tools

class Entry_coreboot_rom(Entry_section):
    """Coreboot ROM image, updated to contain the given payload

    Properties / Entry arguments:
        - coreboot-rom-path: Filename of the coreboot ROM to use (entry
            argument, e.g. from the COREBOOT_ROM environment variable). This
            is a complete flash image as produced by the coreboot build (with
            any Intel descriptor / ME regions already in place), typically
            built with no payload, or with a payload which will be replaced
        - coreboot-filename: Default filename to use if the entry argument is
            not given; it is looked up in the input directories (see
            BINMAN_INDIRS) and defaults to "coreboot.rom"
        - cbfs-name: Name of the payload file in CBFS, defaulting to
            "fallback/payload"
        - cbfs-load-addr: Address to which the payload is loaded. This is
            optional; when omitted it is taken from the payload's SPL ELF
        - cbfs-entry-addr: Entry-point address. This is optional; it defaults
            to the SPL ELF's entry point, or to cbfs-load-addr when that is
            given explicitly
        - cbfs-compress: Compression to use ("lzma", "lz4" or "none"),
            defaulting to lzma

    This supports building U-Boot as a coreboot payload: the coreboot build
    provides everything up to the payload (bootblock, romstage, ramstage,
    FSP, microcode) as a ready-made flash image and this entry inserts the
    payload into its CBFS using the cbfstool bintool, replacing any existing
    file of the same name.

    The contents of this entry, built in the usual way from its subnodes,
    become the payload, added as a flat binary. For example, for a 64-bit
    build entered via SPL::

        rom {
            filename = "u-boot.rom";
            coreboot-rom {
                coreboot-filename = "coreboot.rom";
                cbfs-name = "fallback/payload";

                u-boot-spl {
                };
                u-boot {
                    offset = <0x10000>;
                };
            };
        };

    The load and entry addresses are taken from the u-boot-spl ELF, so they
    do not need to be specified here.

    Note that binman can already create a CBFS and build images containing
    FSP, microcode, XIP stages and an Intel FIT (see chromebook_coral, for
    example), but those are laid out for U-Boot's own use. Building a
    complete coreboot image from pieces is out of scope, though: it would
    mean recreating the Intel descriptor and ME regions, relocating
    coreboot's stages and FSP to their execute-in-place addresses and
    updating the FIT from the microcode within the CBFS. The coreboot build
    already produces all of this, so this entry starts from its finished
    image and only inserts the payload.
    """
    def ReadNode(self):
        super().ReadNode()
        fname, = self.GetEntryArgsOrProps(
            [EntryArg('coreboot-rom-path', str)])
        if not fname:
            fname = fdt_util.GetString(self._node, 'coreboot-filename',
                                       'coreboot.rom')
        self._coreboot_fname = fname
        self._cbfs_name = fdt_util.GetString(self._node, 'cbfs-name',
                                             'fallback/payload')
        self._cbfs_load_addr = fdt_util.GetInt(self._node, 'cbfs-load-addr')
        self._cbfs_entry_addr = fdt_util.GetInt(self._node, 'cbfs-entry-addr')
        self._cbfs_compress = fdt_util.GetString(self._node, 'cbfs-compress',
                                                 'lzma')

    def _FindElfFname(self, entry):
        """Find the ELF filename for an entry, recursing into any subnodes

        Args:
            entry (Entry): Entry to search

        Returns:
            str: ELF filename (e.g. 'spl/u-boot-spl') or None if none is found
        """
        if entry.elf_fname:
            return entry.elf_fname
        entries = entry.GetEntries()
        if entries:
            for subent in entries.values():
                fname = self._FindElfFname(subent)
                if fname:
                    return fname
        return None

    def _GetPayloadAddrs(self):
        """Work out the load and entry addresses for the payload

        The payload is entered via SPL, so when the addresses are not given
        explicitly they are taken from the payload's SPL ELF: the load address
        is the lowest load address in the ELF and the entry address is its
        entry point. This avoids having to hard-code CONFIG_SPL_TEXT_BASE in
        the devicetree.

        Returns:
            tuple:
                int: Load address of the payload
                int: Entry address (where execution starts)

        Raises:
            ValueError: No load address is given and there is no ELF payload
                to derive it from
        """
        load = self._cbfs_load_addr
        entry = self._cbfs_entry_addr
        if load is None:
            elf_fname = None
            for subent in self.GetEntries().values():
                elf_fname = self._FindElfFname(subent)
                if elf_fname:
                    break
            if not elf_fname:
                self.Raise("Missing 'cbfs-load-addr' and no ELF payload to "
                           'derive it from')
            info = elf.DecodeElf(
                tools.read_file(tools.get_input_filename(elf_fname)), 0)
            load = info.load
            if entry is None:
                entry = info.entry
        if entry is None:
            entry = load
        return load, entry

    def BuildSectionData(self, required):
        """Build the ROM by inserting the payload into the coreboot image

        The subnodes of this entry are built into the payload in the same way
        as a normal section, then inserted into a copy of the coreboot image
        using cbfstool.

        Args:
            required (bool): True if the data must be present, False if it is
                OK to return None

        Returns:
            bytes: Contents of the ROM (or None if not ready)
        """
        payload = super().BuildSectionData(required)
        if payload is None:
            return None

        pathname = tools.get_input_filename(self._coreboot_fname,
                                            self.section.GetAllowMissing())
        if not pathname:
            self.missing = True
            return tools.get_bytes(0, 1024)
        uniq = self.GetUniqueName()
        rom_fname = tools.get_output_filename(f'coreboot-rom.{uniq}')
        tools.write_file(rom_fname, tools.read_file(pathname))
        payload_fname = tools.get_output_filename(f'payload.{uniq}')
        tools.write_file(payload_fname, payload)

        load_addr, entry_addr = self._GetPayloadAddrs()

        # The coreboot image may or may not already have a payload
        self.cbfstool.remove(rom_fname, self._cbfs_name)
        if self.cbfstool.add_flat_binary(
                rom_fname, self._cbfs_name, payload_fname,
                load_addr, entry_addr, self._cbfs_compress) is None:
            self.record_missing_bintool(self.cbfstool)
            return tools.get_bytes(0, 1024)

        return tools.read_file(rom_fname)

    def SetImagePos(self, image_pos):
        """Set the position in the image

        The subnodes are positioned within the payload (which is loaded to
        its link address at run time), not within the ROM, so give them
        positions relative to the payload, as for a standalone image. The
        binman symbols in SPL then match those of the image produced by a
        plain build, so SPL can find U-Boot.
        """
        Entry.SetImagePos(self, image_pos)
        for entry in self.GetEntries().values():
            entry.SetImagePos(0)

    def CheckEntries(self):
        # The subnodes make up the payload and are positioned relative to its
        # load address (see SetImagePos), not placed within the coreboot ROM.
        # So the section's overlap and bounds checks do not apply; skip them.
        pass

    def CheckMissing(self, missing_list):
        # Check the coreboot ROM itself (an external image, which sets
        # self.missing when absent) as well as the payload subnodes. The base
        # implementation covers only the ROM and the section only the subnodes,
        # so both are needed here.
        Entry.CheckMissing(self, missing_list)
        super().CheckMissing(missing_list)

    def ProcessContents(self):
        # The payload may have changed, e.g. due to WriteSymbols()
        ok = super().ProcessContents()
        data = self.BuildSectionData(True)
        ok2 = self.ProcessContentsUpdate(data)
        return ok and ok2

    def AddBintools(self, btools):
        super().AddBintools(btools)
        self.cbfstool = self.AddBintool(btools, 'cbfstool')
