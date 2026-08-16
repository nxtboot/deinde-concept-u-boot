.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: rm (command)

rm command
==========

Synopsis
--------

::

    rm <path>

Description
-----------

The rm command deletes a file at the given path in the VFS. The
filesystem must be mounted first using ``mount``.

Not all filesystems support file deletion. If the filesystem does not
implement the unlink operation, the command returns an error.

path
    absolute or relative path to the file to delete

Example
-------

::

    => mount hostfs /host
    => rm /host/old-file.txt
    => mount -t ext4 host 0:1 /mnt
    => rm /mnt/temp.bin

Configuration
-------------

The rm command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the file was deleted,
1 (false) otherwise.

See also
--------

* :doc:`fatrm<fatrm>` for the FAT-only form of this command
* :doc:`mkdir<mkdir>` for creating a directory
* :doc:`mount<mount>` for making a filesystem available to the VFS
