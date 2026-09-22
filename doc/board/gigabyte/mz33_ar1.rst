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

Known limitations
-----------------

Only the PCI devices on bus 0 are enumerated at present. The PCIe root ports
appear as extra functions of the bus 0 bridges and are not scanned, so
devices behind them (NVMe, the USB controllers) are not yet available to
U-Boot.
