.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fatsize (command)

fatsize command
===============

Synopsis
--------

::

    fatsize <interface> <dev[:part]> <filename>

Description
-----------

The fatsize command determines the size of a file on a FAT filesystem and
stores it, in hexadecimal, in the environment variable filesize. Nothing is
printed on success.

If the file cannot be found, the filesize variable is left unchanged, so a
stale value from an earlier command may still be present. Check the return
value rather than assuming filesize describes the file just asked about.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

filename
    path to file

Note that you can use the filesystem-generic :doc:`size command <size>`
instead.

Example
-------

This uses a FAT image bound to the sandbox host interface::

    => fatwrite host 0 1000000 data.bin 40
    64 bytes written in 0 ms
    => fatsize host 0 data.bin
    => echo $filesize
    40

A missing file sets the return value but prints nothing::

    => fatsize host 0 missing.bin
    => echo $?
    1

Configuration
-------------

The fatsize command is only available if CONFIG_CMD_FAT=y.

Return value
------------

The return value $? is set to 0 (true) if the size was determined,
1 (false) otherwise.

See also
--------

* :doc:`fatload<fatload>` for reading a file into memory, which also sets
  filesize
* :doc:`fatwrite<fatwrite>` for creating the file in the first place
* :doc:`fatinfo<fatinfo>` for showing which filesystem is on the device
* :doc:`size<size>` for the same operation on any supported filesystem
* *fatls* for listing files with their sizes
