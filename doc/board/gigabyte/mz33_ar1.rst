.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Simon Glass <sjg@chromium.org>

Gigabyte MZ33-AR1
=================

The MZ33-AR1 is a single-socket server board for the AMD EPYC 9005 ('Turin')
family, supported by Dasharo's coreboot fork with AMD's openSIL. U-Boot runs
on it as the coreboot payload: AMD's PSP and ABL train the memory before any
x86 code runs, coreboot initialises the platform and enters the payload in
32-bit protected mode, and U-Boot takes over from there. There is nothing
for an SPL to do, so the gigabyte_mz33_ar1_cb build is a single 64-bit
U-Boot with no SPL (CONFIG_X86_RUN_64BIT_NO_SPL) which starts with a little
32-bit code of its own (CONFIG_X86_32BIT_ENTRY) to set up page tables and
switch to 64-bit mode.

The board was brought up with an EPYC 9175F and Dasharo v0.9.0.

Building
--------

The coreboot image is not included in the U-Boot tree. Build Dasharo for the
board as described in its documentation (https://docs.dasharo.com, variant
gigabyte_mz33-ar1), or use a release image. The important parts are:

* the coreboot tag for the release, e.g. gigabyte_mz33_ar1_v0.9.0, with its
  submodules
* the AMD PSP firmware set for Turin (Turin.zip from the Dasharo release
  directory), unpacked into 3rdparty/blobs/soc/amd/ so that fw.cfg is at
  3rdparty/blobs/soc/amd/Turin/fw.cfg
* the Dasharo SDK container. Its image is not public but the Dockerfile is,
  at https://github.com/Dasharo/dasharo-sdk, so it can be built locally
* the board's own config, configs/config.gigabyte_mz33-ar1

The payload in that image does not matter, since binman replaces it. Name
the image `coreboot.rom` in one of the binman input directories (or set
COREBOOT_ROM to its path) and build::

   export BINMAN_INDIRS=/path/to/blobs
   make gigabyte_mz33_ar1_cb_defconfig
   make -j$(nproc)

This produces u-boot.rom, a complete 32MB flash image with U-Boot inserted
as `fallback/payload` in the CBFS, using cbfstool from the coreboot build
(CONFIG_COREBOOT_ROM). The payload is u-boot.bin itself, which can also be
inserted by hand::

   cbfstool coreboot.rom remove -n fallback/payload
   cbfstool coreboot.rom add-flat-binary -f u-boot.bin \
      -n fallback/payload -c lzma -l 0x1110000 -e 0x1110000

Flashing
--------

The easiest way to write the image is through the board's BMC, which flashes
the SPI chip while the host is powered off. The BMC's firmware update takes a
Gigabyte-format update file rather than a raw image; the Dasharo tree
provides util/gigabyte/rbutool to convert a copy of the ROM in place::

   cp u-boot.rom u-boot.rbu
   rbutool -i u-boot.rbu

Then, with the host soft-off and the BMC running, upload the .rbu with
Maintenance -> Firmware Update in the BMC web interface, or use Redfish:
serve the file over HTTP and POST to
/redfish/v1/UpdateService/Actions/SimpleUpdate with the file's URL,
`TransferProtocol` HTTP and `UpdateComponent` BIOS, then wait for the task
to complete. The update takes a few minutes. The BMC's session-based
authentication is needed; basic auth is rejected.

The SPI chip can also be programmed externally, but the board keeps the PSP
active as an SPI master on standby power, so mains must be removed
completely first. See the Dasharo recovery documentation for the board.

Booting
-------

Serial output is on the rear COM port (SuperIO 8250 at I/O port 0x3f8,
115200 baud), which coreboot describes in its tables so U-Boot picks it up
automatically. For very early output, enable the debug UART with::

   CONFIG_DEBUG_UART=y
   CONFIG_DEBUG_UART_BASE=0x3f8
   CONFIG_DEBUG_UART_CLOCK=1843200
   CONFIG_SYS_NS16550_PORT_MAPPED=y

The PSP's memory training takes several minutes on the first boot; the
'DDR training passed' line is the last output before coreboot hands over to
U-Boot. U-Boot then reports the memory from the coreboot tables (all of it,
including the bank above 4GB), finds the framebuffer set up by coreboot and
reaches a prompt.

Later boots can skip the training, but only if two things hold. The ABL
restores its saved memory context only when the image carries no 'RW'
APCB (the small instance 0 and 1 blobs, data_snp.apcb and data_snp1.apcb
in the coreboot tree): Dasharo v0.9.0 still includes them, so that release
retrains on every boot, and a rebuild with APCB_SOURCES and APCB_SOURCES1
left unset in the mainboard Makefile fixes it. coreboot keeps the saved
context (the APOB, about 300KB) in the RW_MRC_CACHE region of the flash,
and a BMC firmware update rewrites that region with whatever the image
holds there, so an image meant for repeated flashing should carry a copy
of the APOB at that offset (0xf30000 in the 32MB image); the raw APOB can
be read from DRAM at 0x7010000 with 'md'. With both in place the prompt
appears about a minute after power-on instead of five.

Running U-Boot natively
-----------------------

U-Boot can also replace coreboot altogether, as the 'BIOS reset image' the
PSP loads: build gigabyte_mz33_ar1_defconfig, which produces
u-boot-mz33.bin, a 1MB image laid out for its DRAM address. On this
platform the PSP's ABL trains the memory and sets up the memory map before
any x86 code runs, then decompresses the BIOS image into DRAM and starts
the boot CPU in 16-bit real mode at the reset vector in the image's last
16 bytes, so U-Boot runs from DRAM from the start and needs no SPL. Its
early code only opens the path to the BMC's SuperIO UART, takes the TSC
rate from the P-state MSRs and the memory size from the TOP_MEM MSRs, and
a prompt appears about five minutes after power-on, most of it memory
training, or about a minute when the ABL can restore the saved context (see
above).

U-Boot then does the rest of what openSIL and coreboot would:

* loads the CPU microcode patch, which binman places in the image from the
  cpu_microcode_<rev>.bin files that the Dasharo image carries
* sets up the eight PCIe root complexes, trains the PCIe and SATA links
  through the MPIO firmware, enables each root complex's I/O APIC and
  routes its bridges' legacy interrupts to it, and lets the USB and SATA
  controllers' interrupts out of the NBIF
* programs the FCH's interrupt routing and its ACPI hardware, which the ABL
  only sets up on some boots
* releases the other CPU threads through the SMU and gives each the boot
  CPU's microcode, memory map and MTRRs
* writes ACPI tables (FADT, MADT, MCFG, HPET, IVRS for the IOMMUs, a DSDT
  describing the root complexes and the serial port, and an SSDT with the
  CPUs and the bridges' interrupt routing)

This is enough to boot Ubuntu 24.04 from the NVMe drive, using a BLS entry
on its root filesystem, with all 32 CPUs, networking, USB and a login on
the serial port. For bring-up there are 'smn' (System Management Network
access), 'mca' (machine-check banks), 'fch' (the FCH's interrupt routing
and power-management registers), 'ioapic' and 'pci intr'.

Binman builds the whole flash image as ``u-boot.rom`` with an ``amd-fw`` entry
for the firmware which the PSP reads. That starts with the Embedded Firmware
Structure at offset ``0x20000`` and holds the PSP and BIOS directories, AMD's
firmware, the board's configuration and the BIOS image, compressed. This is the
same layout that coreboot's amdfwtool produces for Dasharo, byte for byte. The
image also holds the regions which the PSP keeps its own data in. The AMD
firmware and the board's configuration are not part of U-Boot, so binman needs
these in its input directories, which are set with ``BINMAN_INDIRS`` in the
environment:

* AMD's firmware for Turin, from ``3rdparty/blobs/soc/amd/Turin`` in Dasharo's
  coreboot tree (``Turin.zip`` from the Dasharo release directory), as listed
  in ``arch/x86/dts/turin-psp.dtsi``
* the board's configuration for the ABL in the files ``data_rec.apcb`` and
  ``data_rec1.apcb`` and ``data_rec2.apcb`` along with ``early_vga.bin`` from
  ``src/mainboard/gigabyte/mz33-ar1`` in the same tree
* the microcode patches in the ``cpu_microcode_<rev>.bin`` files, as carried by
  the Dasharo image
* optionally a saved copy of the ABL's memory context in ``apob-nv.bin`` so that
  the first boot after flashing does not retrain the memory (see above)

For example::

   export BINMAN_INDIRS="/path/to/coreboot/3rdparty/blobs/soc/amd/Turin \
      /path/to/coreboot/src/mainboard/gigabyte/mz33-ar1 /path/to/ucode"
   make gigabyte_mz33_ar1_defconfig
   make -j$(nproc)

The result is flashed as above. The BIOS image is also written to the file
``u-boot-mz33.bin`` on its own. Note that the destination plus the size
must end on a 64KB boundary and the reset vector must be the last 16 bytes
of the image, which is what the defconfig's CONFIG_PPL_TEXT_BASE,
CONFIG_RESET_SEG_START, CONFIG_SYS_X86_START16 and CONFIG_RESET_VEC_LOC
arrange.

Comparing with openSIL
----------------------

When something works under coreboot (openSIL) but not natively, the most
direct way to find the difference is to trace the register writes of both.
With CONFIG_TURIN_REG_TRACE, U-Boot prints a line on the debug UART for
every SMN, PCI-configuration, MMIO and MSR write and every message to the
SMU and MPIO firmware, for example::

   T S 0 13b10044 000001e0
   T P32 00:18.4 08c 00000029
   T Q 0 00000026 1a640084 ffffffff 00400009 00000000 00000000 00000000

openSIL prints the same lines with a trace added to its few access
primitives: xUSLSmnWrite() and xUSLSmnWrite8() in SmnAccess.c, the
xUSLPciWrite*() functions in PciOps.c (skipping the SMN index and data
registers, which the SMN trace covers), the xUSLMemWrite*() functions in
Mmio.h, xUslWrMsr() in CpuLib.h, MpioServiceRequestCommon() and
SmuServiceRequestBrh(), using XUSL_TRACEPOINT() at SIL_TRACE_WARNING level
so that coreboot's normal log level shows them. Then::

   tools/turin_trace.py parse coreboot.log coreboot.trace
   tools/turin_trace.py parse u-boot.log u-boot.trace
   tools/turin_trace.py compare coreboot.trace u-boot.trace names

lists the registers each firmware writes and the other does not, and those
whose final values differ, with names from openSIL's headers.

Building with openSIL
---------------------

U-Boot can also leave the silicon set-up to AMD's openSIL library, as
Dasharo's coreboot does. The ``gigabyte_mz33_ar1_opensil_defconfig`` build
enables ``CONFIG_TURIN_OPENSIL`` so that U-Boot builds openSIL from the tree
given by ``CONFIG_TURIN_OPENSIL_PATH`` with its own compiler and links it in.

U-Boot then gives openSIL its memory, fills in its input blocks and runs its
three timepoints. The first, in place of U-Boot's own code, sets up the data
fabric, the SMU, the root complexes and their links, the CPU complexes,
starting the other threads, and the FCH; the second runs once PCI is
enumerated and the third once U-Boot is otherwise done. The input blocks take
Dasharo's settings, with the board's own from the devicetree: the MPIO links
from the ``/mpio`` node as U-Boot's native code uses them, the BMC's link from
its ``amd,bmc-lane`` property and the USB and SATA settings from the ``/fch``
node. U-Boot still takes each root bus's windows from the fabric, which
openSIL programs, finds the CPUs which openSIL started, and writes the ACPI
and SMBIOS tables and the memory map, which openSIL leaves to its host.

The tree is a checkout of openSIL's turin_poc branch from
https://github.com/openSIL/openSIL which is the same one that Dasharo's
coreboot uses as ``src/vendorcode/amd/opensil/turin_poc/opensil`` in its own
tree. The build needs the ``meson``, ``ninja``, ``nasm`` and ``python3`` tools.
It runs openSIL's Kconfig and meson in U-Boot's output directory without
writing to the tree, and ninja rebuilds whatever has changed, so a change to
openSIL's source gives a new image at the next build. openSIL needs only
``memset()`` and ``memcpy()`` as well as ``assert()`` from its host, which come
from the two headers in ``arch/x86/cpu/turin/opensil/include`` in place of
coreboot's. To build with buildman::

   buildman -a 'TURIN_OPENSIL_PATH="/path/to/opensil"' \
      gigabyte_mz33_ar1_opensil

Without the tree the build fails, since U-Boot would not be able to set up the
SoC. A build which must go ahead anyway, such as CI's world build, can set
``OPENSIL_ALLOW_MISSING=1`` in the environment; the build then warns and
produces a U-Boot which does not work on the board.

openSIL is built for size and position-independent, without SSE and with the
small code model, like the rest of U-Boot. Even so it adds about 280KB, which
does not fit in the 1MB BIOS image, the most the PSP will start. So this build
boots through SPL: the BIOS image holds SPL, which opens the path to the UART,
takes the TSC rate and memory size from the MSRs, then copies U-Boot proper
from the flash, which is mapped just below 4GB, into DRAM and starts it in
64-bit mode. U-Boot proper reads the microcode patches straight from the
flash. Binman puts the BIOS image in the PSP directory as above, with U-Boot
proper and the microcode patches after the PSP's firmware in ``u-boot.rom``
whose first 16MB the board maps at ``0xff000000`` which is the address given by
``CONFIG_TURIN_IMAGE_ADDR`` here. Binman also writes U-Boot proper's address
into SPL. The BIOS image is written to ``u-boot-mz33.bin`` too, laid out for the
DRAM address given by ``CONFIG_SPL_TEXT_BASE`` in this case.

The board then boots as quickly as the native build: U-Boot proper starts
about 1.6s after SPL, openSIL's first timepoint takes 2.4s and Ubuntu reaches
a login about 90s after power-on, with the NVMe drive, the GPU and both network
ports.

openSIL shares out the MMIO above 4GB among the root complexes, up to a limit
of 2^46 which U-Boot sets because MMIO above that reads as all ones on this
board, although CPUID reports more address bits. Under Dasharo, which enables
SME, openSIL takes the memory-encryption bits off the address size, so its
windows stay below 2^46 there too.

Known limitations
-----------------

The coreboot build writes a TPM2 table although no TPM is fitted (its log
reports 'No TPM device found' on every boot), so the native build has none;
if a TPM module is added to the header, enable CONFIG_TPM_V2 and the table
will be written.

The SMBIOS memory devices come from the ABL's APOB, which holds the SPD of
each module it found, so they are only as complete as the SPDs: the module
date and any manufacturer not in the decoder's short list appear as a JEDEC
id.
