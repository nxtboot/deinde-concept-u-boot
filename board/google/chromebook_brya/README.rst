.. SPDX-License-Identifier: GPL-2.0+

Chromebook brya (Alder Lake)
============================

Brya is the reference design for Alder Lake-based Chromebooks; devices such
as felwinter are variants of it. U-Boot runs on these as a coreboot payload:
build with `chromebook_brya_cb_defconfig` (the `_cb` suffix distinguishes
this coreboot-payload build from a future native brya build) and binman
produces `u-boot.rom`, a complete 32MB flash image with U-Boot inserted as
the payload of a coreboot image (see the coreboot-rom binman entry type).

The coreboot image is not included in the U-Boot tree. It is a complete
coreboot build for the brya variant in use, including the flash descriptor
and Intel ME regions, built with CONFIG_PAYLOAD_NONE. Point the build at it
in one of two ways:

- set the COREBOOT_ROM environment variable to its full path, or
- name it `coreboot.rom` in one of the binman input directories (e.g. add
  its directory to BINMAN_INDIRS)

If neither is provided the build still succeeds, but binman reports the
missing image and u-boot.rom is not functional.

If the image will be emulated by an EM100, build coreboot with CONFIG_EM100
so that it uses slower SPI timings. The flash descriptor can be extracted
from the stock felwinter firmware with 'ifdtool -p adl -x'.

The flash image can be written to the board with an EM100 flash emulator::

    em100 -s -p LOW -c W25Q256JW -d u-boot.rom -r

after which the AP must be reset, e.g. with 'sysrst pulse' on the Cr50
console.
