.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fatwrite (command)

fatwrite command
================

Synopsis
--------

::

    fatwrite <interface> <dev[:part]> <addr> <filename> <bytes> [<offset>]

Description
-----------

The fatwrite command writes data from memory to a file on a FAT filesystem,
creating the file if it does not already exist. Note that you can use the
:doc:`save command <save>` instead.

The parent directory must exist; fatwrite does not create it. Use
:doc:`fatmkdir<fatmkdir>` for that.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

addr
    memory address to read the data from

filename
    path to the file to write

bytes
    number of bytes to write

offset
    byte position in the file to start writing at, defaulting to 0

addr, bytes and offset are hexadecimal numbers.

Writing at an offset updates the file in place. The file only grows if the
data written extends beyond the end, so writing 0x10 bytes at offset 0x30 of
a 0x40-byte file leaves it 0x40 bytes long.

Example
-------

This uses a FAT image bound to the sandbox host interface::

    => mw.b 1000000 5a 40
    => fatwrite host 0 1000000 data.bin 40
    64 bytes written in 0 ms
    => fatls host 0
           64   data.bin

    1 file(s), 0 dir(s)

Writing into a directory which does not exist fails, as does writing more
than the filesystem can hold::

    => fatwrite host 0 1000000 nodir/data.bin 40
    nodir: doesn't exist (-2)
                fs_write() ** Unable to write file nodir/data.bin **
    => fatwrite host 0 1000000 big.bin 200000
    Error: no space left: 2097152
    Error: writing contents
                fs_write() ** Unable to write file big.bin **

Configuration
-------------

The fatwrite command is only available if CONFIG_CMD_FAT=y and
CONFIG_FAT_WRITE=y.

Return value
------------

The return value $? is set to 0 (true) if the data was written,
1 (false) otherwise.

See also
--------

* :doc:`fatload<fatload>` for reading the data back into memory
* :doc:`fatsize<fatsize>` for finding the resulting file's size
* :doc:`fatmkdir<fatmkdir>` for creating the directory to write into
* :doc:`save<save>` for the same operation on any supported filesystem
