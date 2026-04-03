.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: mount (command)

mount command
=============

Synopsis
--------

::

    mount [<dev> <mountpoint>]
    mount <iface> <dev:part> <mountpoint>
    mount -t <type> <iface> <dev:part> <mountpoint>

Description
-----------

The mount command attaches a filesystem to the VFS namespace at the given
mount point. On first use the VFS is automatically initialised with an
empty root filesystem at ``/``.

Without arguments, lists all current mounts showing the mount point and
the name of the UCLASS_FS device.

With two arguments, mounts the named UCLASS_FS device at *mountpoint*.

With three arguments, auto-detects the filesystem type on the block
device and mounts it. Each registered VFS filesystem driver is tried in
turn until one succeeds.

With ``-t``, specifies the filesystem type explicitly instead of
auto-detecting.

The ``mount`` command is also available as ``fs mount``; see :doc:`fs`.

dev
    name of a UCLASS_FS device (e.g. ``hostfs`` for the sandbox host
    filesystem)

mountpoint
    absolute path where the filesystem should appear (must start with
    ``/``)

iface
    block device interface (e.g. ``host``, ``mmc``, ``scsi``)

dev\:part
    device and partition number (e.g. ``0:0``, ``0:1``)

type
    filesystem type (e.g. ``ext4``, ``fat``)

Example
-------

Mount the sandbox host filesystem::

    => mount hostfs /host
    => mount
    /host                hostfs

Auto-detect a filesystem on a block device::

    => host bind 0 /tmp/ext4.img
    => mount host 0:0 /mnt
    => ls /mnt
    DIR          0 .
    DIR          0 ..
            12 testfile.txt

Configuration
-------------

The mount command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the mount succeeded or the
mount list was printed, 1 (false) otherwise.
