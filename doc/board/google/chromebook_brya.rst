.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Simon Glass <sjg@chromium.org>

Chromebook Brya
===============

Brya is the reference design for Chromebooks using the Intel Alder Lake
platform (ADL-P), with many variants in the field. This port was brought up
on the Felwinter variant. U-Boot runs natively, with no coreboot: FSP-T sets
up Cache-as-RAM, TPL and SPL execute in place from the SPI flash, FSP-M
trains the memory and U-Boot proper reaches a prompt.

There is also a chromebook_brya_cb build which runs U-Boot as a coreboot
payload; this documentation covers only the native chromebook_brya build.

Building
--------

First, you need the following binary blobs:

   * descriptor.bin - Intel flash descriptor
   * me.bin - Intel Management Engine (the CSE region)
   * fsp_T.bin, fsp_M.bin, fsp_S.bin - the Intel FSP components
   * vbt.bin - Video BIOS Table, for setting up the display
   * mu_06-9a-04.bin - microcode updates for the CPU
   * spd.bin - SPD data for the memory parts which may be fitted

The descriptor and ME come from a stock felwinter firmware image: dump the
flash (or take a recovery image) and extract the two regions with ifdtool
or by offset from the flash map.

The FSP components come from splitting the RaptorLake-P FSP release (which
covers Alder Lake-P as well) using the SplitFspBin.py tool from the FSP
repository::

   python3 SplitFspBin.py split -f Fsp.fd

The microcode file holds the 06-9a-04 updates for both steppings
concatenated, taken from the intel-microcode repository.

The spd.bin file holds the SPD data for the possible memory parts,
concatenated as 0x200-byte entries in DRAM-ID order, matching the memory
parts list in the coreboot brya variant tree.

Put all of these in a directory and point binman at it, then build::

   export BINMAN_INDIRS=/path/to/blobs
   make chromebook_brya_defconfig
   make -j$(nproc)

This produces u-boot.rom, a complete 32MB flash image.

Flashing
--------

The image can be written to the SPI flash with an external programmer, or
run from an EM100 flash emulator. Note that writing a fresh image discards
the CSE's data region, so the first boot after flashing takes longer while
the CSE rebuilds it.

Boot flow
---------

The CPU loads its microcode from the Intel FIT (which binman builds using
the fit,microcode property), then runs the 16-bit and 32-bit init from the
top of flash. TPL calls FSP-T's TempRamInit to set up Cache-as-RAM and
provides an early console on the LPSS UART (routed to the Cr50's CCD
console on a Chromebook). SPL sets up the platform registers which FSP-M
relies on (ACPI base, PWRM, P2SB, system-agent BARs, SMBus/TCO, eSPI
decodes), reads the DRAM-ID straps to select the SPD data and calls FSP-M
to train the memory. U-Boot proper then runs, currently with FSP-S
(silicon init) skipped since it hangs in the graphics power-management
init.

Every boot currently performs a full memory training, which takes around a
minute; the MRC-cache support to avoid this is present but not yet enabled
(the save path hangs and needs investigation).

Known issues
------------

Changing the size of the TPL code can stop the board booting, with no
console output at all. The writable copy of the of-platdata device data
lives in Cache-as-RAM and can be silently lost when the layout of the
execute-in-place code changes which cache lines it occupies; the serial
device's platform data is the usual victim, so TPL hangs in the serial
probe by writing to a zero base address. This needs a proper fix in the
temporary-RAM setup. If the board stops booting after an
apparently-harmless TPL change, suspect this first.

Recovery
--------

If the CSE gets into a bad state (for example after a failed global
reset), boot can hang in FSP-M. Recover with 'ecrst pulse' on the Cr50
console, which resets the EC and the CSE together, then reboot.

Current status
--------------

The board boots to a U-Boot prompt with the memory trained. FSP-S is
skipped, so the display and PCIe root ports are not available yet; PCI
auto-configuration is left off since the FSP and the earlier phases assign
the BARs U-Boot needs.
