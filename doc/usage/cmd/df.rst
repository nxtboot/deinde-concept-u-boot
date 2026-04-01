.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: df (command)

df command
==========

Synopsis
--------

::

    df [<path>]

Description
-----------

The df command displays filesystem usage statistics. With no arguments
it shows all mounts. With a path, it shows the filesystem containing
that path.

Not all filesystems support statfs. ext4 provides full statistics;
FAT and sandbox hostfs show dashes.

path
    absolute or relative path within a mounted filesystem (optional)

Example
-------

::

    => mount -t ext4 host 0:0 /mnt
    => mount hostfs /host
    => df
    Filesystem        Blksz      Total       Used       Free  Size
    /mnt               4096    2097152     106496    1990656
    /host                 -          -          -          -
    => df /mnt
    Filesystem        Blksz      Total       Used       Free  Size
    /mnt               4096    2097152     106496    1990656  2.0 MiB

Configuration
-------------

The df command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the statistics were
retrieved, 1 (false) otherwise.
