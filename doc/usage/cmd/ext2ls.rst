.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ext2ls (command)

ext2ls command
==============

Synopsis
--------

::

    ext2ls <interface> <dev[:part]> [directory]

Description
-----------

The ext2ls command lists the contents of a directory on an ext2, ext3 or
ext4 filesystem. The name is historic: U-Boot has a single driver for the
whole family, so ext2ls, ext4ls and the filesystem-generic
:doc:`ls command <ls>` all reach the same code and read any of the three.

Entries are shown in the order the directory holds them, which is not
alphabetical and not stable across filesystems. A directory is shown with a
trailing slash and no size; a file is shown with its size in bytes, in
decimal. The . and .. entries are listed like any other.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

directory
    path to the directory to list, defaults to /

Example
-------

This uses an ext4 image bound to the sandbox host interface, holding a file
hello.txt of 14 bytes and a directory docs::

    => ext2ls host 0
                docs/
                ./
           14   hello.txt
                ../
                lost+found/
    => ext2ls host 0 /docs
            5   note.txt
                ./
                ../

A directory which does not exist prints nothing, so the return value is the
only way to tell it apart from an empty one::

    => ext2ls host 0 /nodir
    => echo $?
    1

The same happens when the path names a file rather than a directory::

    => ext2ls host 0 hello.txt
    => echo $?
    1

A device which is not there is reported before the filesystem is touched::

    => ext2ls host 1
    ** Bad device specification host 1 **
    Couldn't find partition host 1

Configuration
-------------

The ext2ls command is only available if CONFIG_CMD_EXT2=y.

Return value
------------

The return value $? is set to 0 (true) if the directory was listed,
1 (false) otherwise.

See also
--------

* :doc:`ext2load<ext2load>` for reading one of the listed files into memory
* *ext4ls* for the same command under its ext4 name
* :doc:`ls<ls>` for listing a directory on any supported filesystem
* :doc:`size<size>` for asking about one file rather than listing them all
* *fatls* for the same operation on a FAT filesystem
