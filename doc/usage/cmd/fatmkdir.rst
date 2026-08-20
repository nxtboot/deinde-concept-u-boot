.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fatmkdir (command)

fatmkdir command
================

Synopsis
--------

::

    fatmkdir <interface> <dev[:part]> <directory>

Description
-----------

The fatmkdir command creates a directory on a FAT filesystem. Nothing is
printed on success. Note that you can use the :doc:`mkdir command <mkdir>`
instead.

Only one level is created at a time, so every component of the path except
the last must already exist.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

directory
    path to the directory to create

Example
-------

This uses a FAT image bound to the sandbox host interface. The new directory
is listed with a trailing slash, and starts out holding only the . and ..
entries::

    => fatmkdir host 0 docs
    => fatls host 0
                docs/

    0 file(s), 1 dir(s)
    => fatwrite host 0 1000000 docs/note.bin 10
    16 bytes written in 0 ms
    => fatls host 0 docs
                ./
                ../
           16   note.bin

    1 file(s), 2 dir(s)

Creating a directory which is already there fails::

    => fatmkdir host 0 docs
    docs: already exists
                do_mkdir() ** Unable to create a directory "docs" **

Configuration
-------------

The fatmkdir command is only available if CONFIG_CMD_FAT=y and
CONFIG_FAT_WRITE=y.

Return value
------------

The return value $? is set to 0 (true) if the directory was created,
1 (false) otherwise.

See also
--------

* :doc:`fatwrite<fatwrite>` for writing files into the new directory
* :doc:`fatrm<fatrm>` for removing the directory again once it is empty
* :doc:`mkdir<mkdir>` for the same operation on any supported filesystem
* :doc:`fatinfo<fatinfo>` for showing which filesystem is on the device
* *fatls* for listing the contents of a directory
