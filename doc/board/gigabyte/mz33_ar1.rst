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

The image goes into the PSP directory in place of coreboot's bootblock,
using tools from the Dasharo coreboot build (build/util/cbfstool/amdcompress
and build/util/amdfwtool/amdfwtool) and the same amdfwtool arguments that
build uses (get them with 'make -n -B V=1 build/amdfw.rom'), changing only
the image, its destination and its size::

   amdcompress --infile u-boot-mz33.bin --outfile u-boot.img --compress \
      --maxsize 0x100000
   amdfwtool ...other arguments as in the coreboot build... \
      --bios-bin u-boot.img --bios-bin-dest 0x7150000 \
      --bios-uncomp-size 0x100000 --output amdfw.rom

Then replace the PSP directory in a flash image built for the board, at
the position the coreboot build put it, removing the coreboot stages and
payload which are no longer used (and make room for the larger image)::

   cbfstool coreboot.rom remove -n apu/amdfw
   cbfstool coreboot.rom remove -n fallback/payload
   cbfstool coreboot.rom remove -n fallback/ramstage
   cbfstool coreboot.rom remove -n fallback/romstage
   cbfstool coreboot.rom add -f amdfw.rom -n apu/amdfw -t amdfw -b 0x17800

The result is flashed as above. Note that the destination plus the size
must end on a 64KB boundary and the reset vector must be the last 16 bytes
of the image, which is what the defconfig's CONFIG_TEXT_BASE,
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
