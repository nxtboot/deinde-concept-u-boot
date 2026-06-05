.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Andy Shevchenko <andriy.shevchenko@linux.intel.com>

Edison
======

Building U-Boot
---------------

Build U-Boot for the Edison::

   $ make edison_defconfig
   $ make

This produces ``u-boot.bin`` (the raw U-Boot binary) and ``u-boot-edison.img``
(the full image, with the OSIP header that the mask ROM expects, used by the
xFSTK recovery flow below).

.. note::

   The mask ROM loads the OS image to 0x01100000 but jumps to 0x01101000, so
   U-Boot must sit 4KB into the loaded data. binman pads ``u-boot-edison.img``
   with 4KB of zeroes ahead of U-Boot to provide this gap. The bare
   ``u-boot.bin`` does not contain the padding, so when flashing it directly
   over DFU you must prepend the 4KB yourself (see below).

There are two ways to get U-Boot onto the board:

- over DFU, when the board already runs a working U-Boot -- this is the normal
  case and needs nothing but ``dfu-util``;
- with the xFSTK downloader, when the board has no usable firmware and has to
  be recovered through the mask ROM.

Updating U-Boot over DFU
------------------------

Once a working U-Boot is present, updating it is just a DFU transfer into the
``u-boot0`` eMMC partition; the board boots the new U-Boot on the next reset.

1. Prepend the 4KB alignment gap to ``u-boot.bin``::

     $ { head -c 4096 /dev/zero; cat u-boot.bin; } > u-boot-edison-dfu.bin

2. Reset the board, interrupt autoboot to reach the U-Boot prompt and start
   DFU::

     => run do_dfu_alt_info_mmc
     => dfu 0 mmc 0

   U-Boot also opens this window automatically for a few seconds during boot
   (see ``dfu_to_sec`` and ``do_probe_dfu`` in the environment), so a host can
   flash without stopping at the prompt.

3. From the host, write the image to the ``u-boot0`` partition::

     $ dfu-util -d 8087:0a99 --alt u-boot0 -D u-boot-edison-dfu.bin

4. Stop DFU on the board (Ctrl+C) and reset::

     => reset

Recovering a board with xFSTK
-----------------------------

Use this when the board has no usable U-Boot -- a blank or bricked board, or to
install U-Boot for the first time. It reflashes both the Intel firmware (IFWI)
and U-Boot through the mask ROM, so it needs the Edison recovery image (the DNX
and IFWI blobs) in addition to ``u-boot-edison.img``.

A board with no valid firmware drops into the mask ROM's DnX download mode (USB
ID 8086:e005) automatically on power-up; no recovery straps are needed for that.
The FM/RM straps only force an otherwise-healthy board into recovery.

You need the ``xfstk-dldr-solo`` tool. If your distribution does not package it,
build it from the `xFSTK`_ sources:

.. code-block:: sh

  cd xFSTK
  export DISTRIBUTION_NAME=ubuntu20.04
  export BUILD_VERSION=1.8.5
  git checkout v$BUILD_VERSION
  ...

Copy ``xfstk-dldr-solo`` to ``/usr/local/bin`` and
``libboost_program_options.so.1.54.0`` to ``/usr/lib/i386-linux-gnu/``. You
might find this `drive`_ helpful for the recovery image and the libraries.

Download and unpack the Edison recovery image, then, with the board powered
off, run:

.. code-block:: sh

  xfstk-dldr-solo --gpflags 0x80000007 \
      --osimage u-boot-edison.img \
      --fwdnx recover/edison_dnx_fwr.bin \
      --fwimage recover/edison_ifwi-dbg-00.bin \
      --osdnx recover/edison_dnx_osr.bin

and power-cycle the board within about 10 seconds. To check that the board is
in download mode, look for the mask ROM:

.. code-block:: none

  $ lsusb | grep -E "8087|8086"
  Bus 001 Device 004: ID 8086:e005 Intel Corp.

The tool flashes the firmware and U-Boot and the board resets. Within a few
seconds U-Boot starts, and once the DFU alt list appears (USB ID 8087:0a99) the
board is ready for the DFU updates described above.

.. note::

   To run ``dfu-util`` and ``xfstk-dldr-solo`` without ``sudo``, add udev rules
   granting access to the Edison USB IDs, for example::

     SUBSYSTEM=="usb", ATTR{idVendor}=="8086", ATTR{idProduct}=="e005", MODE="0666"
     SUBSYSTEM=="usb", ATTR{idVendor}=="8087", ATTR{idProduct}=="0a99", MODE="0666"

.. _xFSTK: https://github.com/edison-fw/xFSTK
.. _drive: https://drive.google.com/drive/u/0/folders/1URPHrOk9-UBsh8hjv-7WwC0W6Fy61uAJ
