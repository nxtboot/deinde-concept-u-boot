.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: scsiboot (command)

scsiboot command
================

Synopsis
--------

::

    scsiboot [<addr> [<dev>:<part>]]

Description
-----------

The scsiboot command loads an image from the start of a partition on a SCSI
drive and then boots it, in the way the bootm command does. It is the SCSI
member of a small family of commands which share their code: diskboot does
the same for an IDE drive and usbboot for a USB storage device.

The image must sit at the very first block of the partition, with nothing
before it, since the command reads the header from that block alone. Both the
legacy uImage format and FIT are understood; the header gives the size, which
says how many further blocks to read.

Only the load is unconditional. The command boots what it has loaded when the
autostart environment variable is set to yes, and otherwise leaves the image
in memory and sets the loadaddr variable to its address, so that a later bootm
can start it.

The drives have to be found before the command can name one, so run 'scsi
scan' first. Without it the command reports a bad device specification, even
where a drive is attached.

addr
    memory address to load the image to. It defaults to the value of
    CONFIG_SYS_LOAD_ADDR

dev:part
    drive number and partition number, both counted from 0 in the order the
    drives are found and both read as hexadecimal. A partition number of 0
    means the whole drive, and auto means the first bootable partition, or
    the first one where none is bootable. Leaving the partition out selects
    partition 1

    The argument as a whole defaults to the bootdevice environment variable,
    which a '-' also selects, and the command fails where that is unset too

Example
-------

Sandbox emulates a SCSI drive backed by a file, so the command has something
to read. Scan the bus first::

    => scsi scan
    scanning bus for devices...
      Device 0: (0:0) Vendor:  Prod.:  Rev:
                Type: Hard Disk
                Capacity: 2.0 MB = 0.0 GB (4096 x 512)

The emulated drive holds no image, so the read succeeds and the format check
is what fails::

    => scsiboot 1000 0:0

    Loading from scsi device 0, partition 0: Name: Whole Disk  Type: U-Boot
    ** Unknown image type
    => echo $?
    1

With no argument at all the command has no device to work with, since sandbox
selects none by default::

    => scsiboot
    ** No device specified **
    => echo $?
    1

Naming a drive which is not there fails in the same way::

    => scsiboot 1000 99:1
    ** Bad device specification scsi 99 **
    => echo $?
    1

The command takes two arguments at most, so a third produces the usage
message::

    => scsiboot 1000 0:0 1
    scsiboot - boot from SCSI device

    Usage:
    scsiboot loadAddr dev:part
    => echo $?
    1

Return value
------------

The return value $? is 0 (true) if the image is loaded, and 1 (false) if the
drive or partition cannot be found, if a block cannot be read or if the image
format is not recognised.

Where autostart is set to yes the value is that of the boot which follows, so
a failure to start the image is reported rather than the load being called a
success.

Configuration
-------------

The command is only available if CONFIG_CMD_SCSI=y, which needs CONFIG_SCSI=y
for the drivers themselves. CONFIG_LEGACY_IMAGE_FORMAT=y adds support for the
legacy uImage format and CONFIG_FIT=y for FIT, with neither of them being a
requirement of the command.

See also
--------

* *scsi* for scanning the bus and for reading blocks without booting them
* :doc:`bootm<bootm>` for booting an image which is already in memory, which
  is what this command uses once the image is loaded
* :doc:`ide<ide>` for the IDE equivalent of the scsi command
* :doc:`nvme<nvme>` for the same on an NVMe namespace
* :doc:`part<part>` for listing the partitions a drive offers
* :doc:`read<read>` for reading blocks from a partition without treating them
  as an image
* *diskboot* and *usbboot* for the same command on an IDE drive and on a USB
  storage device
