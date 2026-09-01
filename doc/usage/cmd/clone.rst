.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: clone (command)

clone command
=============

Synopsis
--------

::

    clone <src interface> <src dev> <dest interface> <dest dev> <size[K/M/G]>

Description
-----------

The clone command copies raw blocks from one block device to another, without
going through a filesystem or looking at the partition table. It is intended
for flashing a device from another one when neither network nor USB support is
available.

src interface
    interface holding the device to copy from (mmc, scsi, usb, blkmap, ...)

src dev
    number of the device to copy from

dest interface
    interface holding the device to copy to

dest dev
    number of the device to copy to

size
    number of bytes to copy, or 0 to copy to the end of the device. The value
    is read as decimal, unlike the addresses most commands take, and may carry
    a K, M or G suffix for KiB, MiB or GiB

A size of 0 means the size of the larger of the two devices, so a copy which
runs off the end of either one stops with a read or write error.

The copy is done a mebibyte at a time, so the block size of both devices must
divide a mebibyte exactly. Devices which do not qualify are refused with
'failed: cannot match device block sizes'.

The command reports how many bytes it has read and written, followed by the
elapsed time and, where that time is at least a millisecond, the rate.

Example
-------

This copies a mebibyte between two memory-backed :doc:`blkmap<blkmap>`
devices, using a pattern in the source memory to show that the data arrives::

    => blkmap create src
    Created "src"
    => blkmap map src 0 800 mem 1000000
    Block 0x0+0x800 mapped to 0x1000000
    => blkmap create dst
    Created "dst"
    => blkmap map dst 0 800 mem 2000000
    Block 0x0+0x800 mapped to 0x2000000
    => mw.l 1000000 12345678 4
    => md.l 2000000 4
    02000000: 00000000 00000000 00000000 00000000  ................
    => clone blkmap 0 blkmap 1 1M
    Copying 1048576 bytes from blkmap:0 to blkmap:1
    1048576 read
    1048576 written
    0ms
    => md.l 2000000 4
    02000000: 12345678 12345678 12345678 12345678  xV4.xV4.xV4.xV4.

A device which cannot be found is reported before any copying starts::

    => clone bogus 0 blkmap 1 0
    Unable to open source device

Configuration
-------------

The command is only available if CONFIG_CMD_CLONE=y.

Return value
------------

The return value $? is 0 (true) if the whole copy succeeds. It is 1 (false) if
either device cannot be opened, if the two block sizes cannot be matched, or if
a read or write stops the copy early; in that last case the byte counts say how
far it got.

See also
--------

* :doc:`blkmap<blkmap>` for building a virtual block device, which may serve as
  either end of a clone
* :doc:`read<read>` and :doc:`write<write>` for copying a fixed number of
  blocks between memory and a device rather than between two devices
* :doc:`part<part>` for listing the partitions on a device, none of which clone
  pays any attention to
