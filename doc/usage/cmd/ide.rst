.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: ide (command)

ide command
===========

Synopsis
--------

::

    ide reset
    ide info
    ide device [<dev>]
    ide part [<dev>]
    ide read <addr> <blk#> <cnt>
    ide write <addr> <blk#> <cnt>

Description
-----------

The ide command provides access to drives attached to an IDE controller, also
known as PATA. It lists the drives which the controller has found, selects the
one which later commands work on, shows its partitions and copies raw blocks
between the drive and memory.

Apart from the reset sub-command it is the same command as scsi, usb and nvme
provide, so the sub-commands and their output are shared with those.

dev
    device number, counted from 0 in the order the drives are found

addr
    memory address to read into or write from

blk#
    first block to transfer, numbered from 0

cnt
    number of blocks to transfer

ide reset
~~~~~~~~~

This removes the controller and its drives, then probes the controller again,
which finds the drives afresh. Use it when a drive is swapped, or when the
controller is left in a state the driver cannot make sense of.

ide info
~~~~~~~~

This lists the drives, giving the vendor, product and revision strings the
drive reports, its type and its capacity. Nothing is printed when no drive is
present.

ide device
~~~~~~~~~~

With no argument this shows the current device; with one it makes that device
current. The read, write and part sub-commands all work on the current device.

ide part
~~~~~~~~

This prints the partition table of the given device, or of every device when
none is given.

ide read
~~~~~~~~

This reads cnt blocks starting at block blk# into memory at addr.

ide write
~~~~~~~~~

This writes cnt blocks from memory at addr to the drive, starting at block
blk#.

Example
-------

Sandbox builds the command but has no IDE controller to attach it to, so the
examples here show what each sub-command does when there is no drive.

Listing the drives prints nothing, since there are none::

    => ide info
    => echo $?
    0

Asking for the current device says so plainly::

    => ide device

    no ide devices available
    => echo $?
    1

The same goes for the partition table::

    => ide part

    no ide partition table available

Selecting a device which does not exist fails::

    => ide dev 0

    Device 0: unknown device
    => echo $?
    1

A transfer prints what it is about to do before finding that the device is
missing, so the line is left unfinished and the command returns 1::

    => ide read 1000 0 1

    ide read: device 0 block # 0, count 1 ...

Resetting the controller reports that there is none::

    => ide reset

    Reset IDE: No IDE controller
    => echo $?
    1

Return value
------------

The return value $? is 0 (true) if the sub-command succeeds and 1 (false) if
it fails, if the sub-command is not recognised or if its arguments are wrong.

Note that 'ide info' returns 0 even when no drive is present, and so does
'ide part', which reports a missing partition table rather than failing.

Configuration
-------------

The command is only available if CONFIG_CMD_IDE=y, which selects CONFIG_IDE
to build the driver. CONFIG_SYS_IDE_MAXBUS and CONFIG_SYS_IDE_MAXDEVICE say
how many buses the controller has and how many drives may be attached to
them.

See also
--------

* :doc:`part<part>` for listing and querying partitions on any block device,
  not just an IDE one
* :doc:`read<read>` for reading blocks from a partition rather than from the
  start of the drive
* :doc:`write<write>` for writing blocks to a partition
* *scsi*, *usb* and *nvme* for the same set of sub-commands on other kinds of
  drive
* *diskboot* for loading and starting an image held on an IDE drive
