.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: mv (command)

mv command
==========

Synopsis
--------

With VFS (CONFIG_CMD_VFS)::

    mv <old_path> <new_path>

Legacy (CONFIG_CMD_FS_LEGACY)::

    mv <interface> [<dev[:part]>] <old_path> <new_path>

Description
-----------

The mv command renames or moves a file or directory within a filesystem.
Both paths must be on the same mounted filesystem - cross-filesystem
moves are not supported.

old_path
    absolute or relative path to the existing file or directory

new_path
    absolute or relative path for the new name/location

Example
-------

VFS::

    => mount hostfs /host
    => mv /host/old-name.txt /host/new-name.txt
    => mv /host/file.txt /host/subdir/file.txt

Legacy::

    => mv mmc 0:0 dir/foo dir/bar

Configuration
-------------

The VFS mv command is available when CONFIG_CMD_VFS=y. The legacy
version is available when CONFIG_CMD_FS_LEGACY=y.

Return value
------------

The return value $? is set to 0 (true) if the rename succeeded,
1 (false) otherwise.
