.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fs (command)

fs command
==========

Synopsis
--------

::

    fs mount [<dev> <mountpoint>]                   - list or create mounts
    fs mount <iface> <dev:part> <mountpoint>        - auto-detect and mount
    fs mount -t <type> <iface> <dev:part> <mountpoint> - mount specific type
    fs umount <mountpoint> | -a                     - unmount a filesystem
    fs ls [<path>]                                  - list directory (default /)
    fs cp <source> <dest>                           - copy a file

Description
-----------

The ``fs`` command provides access to the virtual filesystem (VFS) layer. It
allows filesystems to be mounted at paths within a unified namespace, then
listed and navigated using absolute paths.

On first use the VFS is automatically initialised with an empty root filesystem
at ``/``.

See :doc:`/develop/vfs` for more information about the VFS layer.


fs mount
~~~~~~~~

Without arguments, lists all current mounts showing the mount point and the
name of the UCLASS_FS device mounted there.

With two arguments, mounts the filesystem device *dev* at *mountpoint*. The
device is looked up by name in UCLASS_FS.

With three arguments (``iface dev:part mountpoint``), auto-detects the
filesystem type on the block device and mounts it. Each registered VFS
filesystem type is tried in turn.

With ``-t``, specifies the filesystem type explicitly.

dev
    Name of a UCLASS_FS device (e.g. ``hostfs`` for the sandbox host
    filesystem)

mountpoint
    Absolute path where the filesystem should be mounted (must start with
    ``/``)


fs umount
~~~~~~~~~

Unmounts the filesystem at *mountpoint* and removes the corresponding
UCLASS_MOUNT device.

mountpoint
    Path of an existing mount point


fs ls
~~~~~

Lists the contents of a directory. If no path is given, lists the root
directory ``/``.

At the root level, mount points appear as directories. Within a mounted
filesystem, actual directory entries are shown along with any child mount
points.

path
    Absolute path to list (default ``/``)


fs cp
~~~~~

Copies a file from *source* to *dest*. Both paths can be on different
mounted filesystems, enabling cross-filesystem copies. The file is copied
in chunks.

source
    Absolute or relative path to the source file

dest
    Absolute or relative path to the destination file (created if it does
    not exist)


Example
-------

::

    => fs mount hostfs /host
    => fs mount
    /host                hostfs
    => fs ls /
    DIR          0 host
    => fs ls /host
    DIR       4096 arch
    DIR      12288 cmd
       ...
    => fs umount /host

Auto-detect a filesystem on a block device::

    => host bind 0 /tmp/ext4.img
    => fs mount host 0:0 /mnt
    => fs ls /mnt
    DIR      16384 lost+found
    => fs umount /mnt

Configuration
-------------

The ``fs`` command is available when CONFIG_CMD_VFS=y, which depends on
CONFIG_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the command succeeded, 1 (false)
otherwise.
