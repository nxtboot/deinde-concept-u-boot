.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fsuuid (command)

fsuuid command
==============

Synopsis
--------

::

    fsuuid <interface> <dev[:part]> [varname]

Description
-----------

The fsuuid command looks up the UUID recorded inside a filesystem and either
prints it or stores it in an environment variable. This is the identifier the
filesystem carries in its own superblock, which is not the same as the
partition UUID held in the partition table; see :doc:`part<part>` for the
latter.

The filesystem type is detected automatically, so the command works on any
filesystem U-Boot can read. What is reported depends on what the filesystem
records. An ext2, ext3, ext4 or btrfs filesystem carries a full 16-byte UUID
and is shown in the usual 8-4-4-4-12 form. A FAT filesystem has only a
four-byte volume serial number, so it is shown as two groups of four hex
digits.

With no varname the UUID is printed, followed by a newline. With a varname
nothing is printed and the variable is set instead, which is the useful form
for building a kernel command line::

    fsuuid mmc 0:2 uuid && setenv bootargs "root=UUID=${uuid}"

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

varname
    environment variable to set to the UUID, instead of printing it

Example
-------

This uses an ext4 image bound to the sandbox host interface::

    => fsuuid host 0
    38d3ca24-77d7-4245-b769-0a4e2a27be56
    => fsuuid host 0 myvar
    => echo $myvar
    38d3ca24-77d7-4245-b769-0a4e2a27be56

A FAT filesystem reports its shorter volume serial number in the same way::

    => fsuuid host 0
    6BB8-4C49

A device which is not there is reported before the filesystem is touched::

    => fsuuid host 9
    ** Bad device specification host 9 **
    Couldn't find partition host 9
    => echo $?
    1

Configuration
-------------

The fsuuid command is only available if CONFIG_CMD_FS_UUID=y.

Return value
------------

The return value $? is set to 0 (true) if the UUID was read, and to 1 (false)
if the device or partition could not be found, or if the filesystem was read
but holds no UUID.

Note that the variable is left alone when the command fails, so an old value
survives a failed lookup. Test $? rather than assuming the variable has been
replaced.

See also
--------

* :doc:`part<part>` for the partition UUID, which the partition table holds
  rather than the filesystem
* :doc:`fsinfo<fsinfo>` for the block counts of the same filesystem
* :doc:`ls<ls>` for listing what the filesystem contains
* *fstype* for asking which filesystem is on a partition
