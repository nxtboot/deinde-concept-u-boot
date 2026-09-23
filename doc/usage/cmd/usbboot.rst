.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: usbboot (command)

usbboot command
===============

Synopsis
--------

::

    usbboot [<addr> [<dev>:<part>]]

Description
-----------

The usbboot command loads an image from the start of a partition on a USB
storage device and then boots it, in the way the bootm command does. It is the
USB member of a small family of commands which share their code: diskboot does
the same for an IDE drive and scsiboot for a SCSI drive.

The image must sit at the very first block of the partition, with nothing
before it, since the command reads the header from that block alone. Both the
legacy uImage format and FIT are understood; the header gives the size, which
says how many further blocks to read.

Only the load is unconditional. The command boots what it has loaded when the
autostart environment variable is set to yes, and otherwise leaves the image
in memory and sets the loadaddr variable to its address, so that a later bootm
can start it.

The bus has to be enumerated before the command can name a device, so run 'usb
start' first. Without it the command reports a bad device specification, even
where a stick is plugged in.

The device numbers come from the order in which the mass-storage devices are
found, so a 'usb reset' can hand the same stick a different number. Check with
'usb storage' after a reset rather than assuming the numbering has held.

addr
    memory address to load the image to. It defaults to the value of
    CONFIG_SYS_LOAD_ADDR

dev:part
    device number and partition number, both counted from 0 in the order the
    devices are found and both read as hexadecimal. A partition number of 0
    means the whole device, and auto means the first bootable partition, or
    the first one where none is bootable. Leaving the partition out selects
    partition 1

    The argument as a whole defaults to the bootdevice environment variable,
    which a '-' also selects, and the command fails where that is unset too

Example
-------

Sandbox emulates four USB sticks, each backed by a file, so the command has
something to read. Which stick a number refers to varies from run to run, as
the note above says, so the output below may name a different one. Enumerate
the bus first::

    => usb start
    starting USB...
    Bus usb@1: 6 USB Device(s) found
           scanning usb for storage devices... 4 Storage Device(s) found

The first stick holds no image, so the read succeeds and the format check is
what fails::

    => usbboot 1000 0:0

    Loading from usb device 0, partition 0: Name: Whole Disk  Type: U-Boot
    ** Unknown image type
    => echo $?
    1

With no argument at all the command has no device to work with, since sandbox
leaves bootdevice unset::

    => usbboot
    ** No device specified **
    => echo $?
    1

Setting that variable gives the command somewhere to look::

    => setenv bootdevice 0:0
    => usbboot 1000

    Loading from usb device 0, partition 0: Name: Whole Disk  Type: U-Boot
    ** Unknown image type
    => echo $?
    1

Naming a device which is not there fails before any transfer::

    => usbboot 1000 99:1
    ** Bad device specification usb 99 **
    => echo $?
    1

The command takes two arguments at most, so a third produces the usage
message::

    => usbboot 1000 0:0 1
    usbboot - boot from USB device

    Usage:
    usbboot loadAddr dev:part
    => echo $?
    1

Return value
------------

The return value $? is 0 (true) if the image is loaded, and 1 (false) if the
device or partition cannot be found, if a block cannot be read or if the image
format is not recognised.

Where autostart is set to yes the value is that of the boot which follows, so
a failure to start the image is reported rather than the load being called a
success.

Configuration
-------------

The command is only available if CONFIG_CMD_USB=y and CONFIG_USB_STORAGE=y,
the latter being what builds the mass-storage driver the command reads
through. CONFIG_LEGACY_IMAGE_FORMAT=y adds support for the legacy uImage
format and CONFIG_FIT=y for FIT, with neither of them being a requirement of
the command.

See also
--------

* *usb* for enumerating the bus and for reading blocks without booting them
* :doc:`bootm<bootm>` for booting an image which is already in memory, which
  is what this command uses once the image is loaded
* :doc:`scsiboot<scsiboot>` for the same command on a SCSI drive
* :doc:`ide<ide>` for the IDE equivalent of the usb command
* :doc:`nvme<nvme>` for the same on an NVMe namespace
* :doc:`part<part>` for listing the partitions a device offers
* :doc:`read<read>` for reading blocks from a partition without treating them
  as an image
* :doc:`ums<ums>` for going the other way and exporting a device to a host
* *diskboot* for the same command on an IDE drive
