.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ln (command)

ln command
==========

Synopsis
--------

::

    ln <target> <linkname>

Description
-----------

The ln command creates a symbolic link in the VFS. The symbolic link
*linkname* is created pointing to *target*. The target string is stored
as-is in the symlink and is not resolved at creation time.

The filesystem must be mounted first using ``mount`` and must support
symbolic links (e.g. ext4). FAT filesystems do not support symlinks.

target
    string to store as the symlink target

linkname
    absolute or relative path for the new symbolic link

Example
-------

::

    => mount host 0:0 /mnt
    => ln testfile.txt /mnt/link.txt
    => stat /mnt/link.txt
      File: link.txt
      Size: 12
      Type: symbolic link

Configuration
-------------

The ln command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the symlink was created,
1 (false) otherwise.
