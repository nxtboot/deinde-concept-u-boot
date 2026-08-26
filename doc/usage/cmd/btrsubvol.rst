.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: btrsubvol (command)

btrsubvol command
=================

Synopsis
--------

::

    btrsubvol <interface> <dev[:part]>

Description
-----------

The btrsubvol command lists the subvolumes of a BTRFS filesystem. A
subvolume is a separately rooted directory tree inside the same filesystem,
which BTRFS can snapshot and mount on its own. Distributions often keep the
root filesystem and /home in subvolumes of one BTRFS volume, so knowing
which subvolumes exist is the first step in working out what to boot.

One line is printed per subvolume::

    ID <id> gen <generation> path <path>

id
    numeric identifier of the subvolume. The top-level filesystem tree is
    always ID 5 and is always listed; subvolumes created afterwards are
    numbered from 256

generation
    transaction number the subvolume was last written in, which rises as the
    filesystem is modified

path
    path of the subvolume from the top of the filesystem, so the top-level
    tree is shown as /

There are no other BTRFS-specific commands. Listing directories and reading
files is done with the filesystem-generic commands, which recognise BTRFS
like any other supported filesystem.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

Example
-------

This uses a BTRFS image bound to the sandbox host interface. It has just
been created, so only the top-level tree is present::

    => btrsubvol host 0
    ID 5 gen 7 path /

A filesystem which is not BTRFS is rejected without any message, so the
return value is the only sign of what happened::

    => btrsubvol host 0
    => echo $?
    1

A device which is not there is reported before the filesystem is touched::

    => btrsubvol host 9
    ** Bad device specification host 9 **
    Couldn't find partition host 9
    => echo $?
    1

Configuration
-------------

The btrsubvol command is only available if CONFIG_CMD_BTRFS=y, which also
enables the BTRFS filesystem driver.

Return value
------------

The return value $? is set to 0 (true) if a BTRFS filesystem was found on
the given device, and to 1 (false) otherwise.

Note that the listing itself cannot fail the command: an error while walking
the subvolume tree is reported on the console but the return value is still
0.

See also
--------

* :doc:`fsuuid<fsuuid>` for the UUID of the same filesystem
* :doc:`ls<ls>` for listing a directory, which works on BTRFS as on any
  other supported filesystem
* :doc:`part<part>` for finding which partition holds the filesystem
