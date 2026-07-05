.. SPDX-License-Identifier: GPL-2.0+

Luckfox Pico boards
===================

U-Boot for the Luckfox Pico series of RV1103/RV1106 boards. Only the
Pico Mini B (RV1103, 64MB DDR2, 128MB SPI NAND) is supported so far.

Quick Start
-----------

- Get the DDR initialization binary
- Build U-Boot
- Boot the board over USB, or flash U-Boot into the SPI NAND

Get the DDR initialization binary
---------------------------------

.. code-block:: bash

   $ git clone https://github.com/rockchip-linux/rkbin.git

The DDR initialization binary is at rkbin/bin/rv11/rv1106_ddr_924MHz_v1.15.bin
and covers both the RV1103 and the RV1106.

Build U-Boot
------------

.. code-block:: bash

   $ export CROSS_COMPILE=arm-linux-gnueabihf-
   $ export ROCKCHIP_TPL=<path-to-rkbin>/bin/rv11/rv1106_ddr_924MHz_v1.15.bin
   $ make luckfox-pico-mini-b-rv1103_defconfig
   $ make

This produces idbloader.img and u-boot.img for flashing to the SPI
NAND, plus u-boot-rockchip-usb471.bin and u-boot-rockchip-usb472.bin
for booting over USB.

Boot over USB (maskrom mode)
----------------------------

Hold the BOOT button while applying power (or leave the boot media
blank) and the BootROM enters maskrom mode on the USB OTG port,
appearing as USB ID 2207:110c. U-Boot then runs entirely from RAM;
nothing is written to the flash.

Send the two images with a maskrom download tool such as rockusb or
rkdeveloptool (note that the loader format parser in older
rkdeveloptool versions is not needed here, since these are raw
images):

.. code-block:: bash

   $ rockusb download-boot u-boot-rockchip-usb471.bin u-boot-rockchip-usb472.bin

Flash U-Boot into the SPI NAND
------------------------------

Enter maskrom mode as above, then use a loader from rkbin to run the
Rockchip usbplug and write the images:

.. code-block:: bash

   $ cd <path-to-rkbin>
   $ ./tools/boot_merger RKBOOT/RV1106MINIALL.ini
   $ ./tools/upgrade_tool db rv1106_download_*.bin
   $ ./tools/upgrade_tool wl 0x200 idbloader.img
   $ ./tools/upgrade_tool wl 0xa00 u-boot.img

Power cycle the board and U-Boot output appears on UART2 (115200,8N1).
