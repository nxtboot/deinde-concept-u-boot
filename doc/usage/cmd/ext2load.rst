.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ext2load (command)

ext2load command
================

Synopsis
--------

::

    ext2load <interface> [<dev[:part]> [addr [filename [bytes [pos]]]]]

Description
-----------

The ext2load command reads a file from an ext2, ext3 or ext4 filesystem into
memory. As with :doc:`ext2ls<ext2ls>`, the name is historic: one driver
serves the whole family, so ext2load, ext4load and the filesystem-generic
:doc:`load command <load>` all read any of the three.

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

    => ext2load host 0 1000000 hello.txt
    14 bytes read in 2 ms (6.8 KiB/s)
    => echo $filesize $fileaddr
    e 1000000
    => md.b 1000000 e
    01000000: 48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a        Hello, world!.

The rate is left out when the transfer takes less than a millisecond. Giving
bytes and pos reads part of the file::

    => ext2load host 0 1000000 hello.txt 5 7
    5 bytes read in 0 ms
    => md.b 1000000 5
    01000000: 77 6f 72 6c 64                                   world

With no filename, bootfile is used, and with no address, loadaddr::

    => setenv bootfile hello.txt
    => setenv loadaddr 2000000
    => ext2load host 0
    14 bytes read in 0 ms
    => echo $fileaddr
    2000000

A file which is not there is reported by the filesystem layer::

    => ext2load host 0 1000000 missing.txt
                 do_load() Failed to load 'missing.txt'
    => echo $?
    1

Leaving out the filename without a bootfile to fall back on fails before the
filesystem is read::

    => ext2load host 0 1000000
    ** No boot file defined **

Configuration
-------------

The ext2load command is only available if CONFIG_CMD_EXT2=y.

Return value
------------

The return value $? is set to 0 (true) if the file was read, 1 (false)
otherwise. Reading fewer bytes than asked for is not an error: the transfer
stops at the end of the file.

See also
--------

* :doc:`ext2ls<ext2ls>` for finding out which files are there to read
* *ext4load* for the same command under its ext4 name
* :doc:`load<load>` for reading a file from any supported filesystem
* :doc:`size<size>` for asking how big a file is without reading it
* :doc:`fatload<fatload>` for the same operation on a FAT filesystem
