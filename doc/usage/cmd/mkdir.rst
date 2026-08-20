.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: mkdir (command)

mkdir command
=============

Synopsis
--------

::

    mkdir <path>

Description
-----------

The mkdir command creates a directory at the given path in the VFS. The
parent directory must already exist. The filesystem must be mounted
first using ``mount``.

Not all filesystems support directory creation. If the filesystem does
not implement the mkdir operation, the command returns an error.

path
    absolute or relative path to the directory to create

Example
-------

::

    => mount hostfs /host
    => mkdir /host/newdir
    => ls /host
    DIR       4096 newdir
       ...

Configuration
-------------

The mkdir command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the directory was created,
1 (false) otherwise.

See also
--------

* :doc:`fatmkdir<fatmkdir>` for the FAT-only form of this command
* :doc:`rm<rm>` for removing a file or an empty directory
* :doc:`mount<mount>` for making a filesystem available to the VFS
