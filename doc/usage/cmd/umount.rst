.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: umount (command)

umount command
==============

Synopsis
--------

::

    umount <mountpoint> | -a

Description
-----------

The umount command detaches a mounted filesystem from the VFS namespace.

With ``-a``, unmounts all currently mounted filesystems.

The ``umount`` command is also available as ``fs umount``; see :doc:`fs`.

mountpoint
    path of an existing mount point to unmount

Example
-------

::

    => mount host 0:0 /mnt
    => umount /mnt

Unmount all filesystems::

    => umount -a

Configuration
-------------

The umount command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the unmount succeeded,
1 (false) otherwise.
