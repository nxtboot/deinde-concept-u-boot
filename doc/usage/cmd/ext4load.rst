.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ext4load (command)

ext4load command
================

Synopsis
--------

::

    ext4load <interface> [<dev[:part]> [addr [filename [bytes [pos]]]]]

Description
-----------

The ext4load command reads a file from an ext2, ext3 or ext4 filesystem into
memory. One driver serves the whole family, so ext4load,
:doc:`ext2load<ext2load>` and the filesystem-generic
:doc:`load command <load>` all read any of the three; the name says which
command was enabled rather than which filesystem is on the device.

The number of bytes read is saved in the environment variable filesize and
the address they were read to in fileaddr, both in hexadecimal. Neither is
changed if the file cannot be read.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

addr
    load address, defaults to environment variable loadaddr or, if that is
    not set, to configuration variable CONFIG_SYS_LOAD_ADDR

filename
    path to file, defaults to environment variable bootfile

bytes
    maximum number of bytes to load, 0 or omitted meaning the whole file

pos
    byte offset in the file to start reading from, defaulting to 0

part, addr, bytes and pos are hexadecimal numbers.

Example
-------

This uses an ext4 image bound to the sandbox host interface, holding a file
hello.txt which contains ``Hello, world!`` and a newline::

    => ext4load host 0 1000000 hello.txt
    14 bytes read in 1 ms (13.7 KiB/s)
    => echo $filesize $fileaddr
    e 1000000
    => md.b 1000000 e
    01000000: 48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a        Hello, world!.

Giving bytes and pos reads part of the file::

    => ext4load host 0 1000000 hello.txt 5 7
    5 bytes read in 1 ms (4.9 KiB/s)
    => md.b 1000000 5
    01000000: 77 6f 72 6c 64                                   world

Asking for more bytes than the file holds stops at the end of it, and a
position past the end reads nothing at all. Neither is an error::

    => ext4load host 0 1000000 hello.txt 100
    14 bytes read in 2 ms (6.8 KiB/s)
    => ext4load host 0 1000000 hello.txt 5 20
    0 bytes read in 1 ms (0 Bytes/s)

With no filename, bootfile is used, and with no address, loadaddr::

    => setenv bootfile hello.txt
    => setenv loadaddr 2000000
    => ext4load host 0
    14 bytes read in 1 ms (13.7 KiB/s)
    => echo $fileaddr
    2000000

A file which is not there is reported by the filesystem layer::

    => ext4load host 0 1000000 missing.txt
                 do_load() Failed to load 'missing.txt'
    => echo $?
    1

Leaving out the filename without a bootfile to fall back on fails before the
filesystem is read::

    => setenv bootfile
    => ext4load host 0 1000000
    ** No boot file defined **

Configuration
-------------

The ext4load command is only available if CONFIG_CMD_EXT4=y.

Return value
------------

The return value $? is set to 0 (true) if the file was read, 1 (false)
otherwise. Reading fewer bytes than asked for is not an error: the transfer
stops at the end of the file.

See also
--------

* :doc:`ext2load<ext2load>` for the same command under its ext2 name
* *ext4ls* for finding out which files are there to read
* :doc:`ext4size<ext4size>` for asking how big a file is without reading it
* :doc:`load<load>` for reading a file from any supported filesystem
* :doc:`fatload<fatload>` for the same operation on a FAT filesystem
