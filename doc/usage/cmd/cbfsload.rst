.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cbfsload (command)

cbfsload command
================

Synopsis
--------

::

    cbfsload <addr> <filename> [bytes]

Description
-----------

The cbfsload command reads a file from the CBFS (Coreboot filesystem) which
:doc:`cbfsinit<cbfsinit>` has read, into memory.

addr
    address to load the file at, in hexadecimal

filename
    name of the file to read, as :doc:`cbfsls<cbfsls>` lists it

bytes
    maximum number of bytes to read, in hexadecimal. The whole file is read
    where this is left out, or is zero, or is larger than the file

The command prints the name of the file it has found, then the number of bytes
it has read, and sets the *filesize* environment variable to that count in
hexadecimal.

The file data comes from the ROM rather than from the copy of the metadata in
RAM, so the ROM must still hold the archive cbfsinit read. The name is matched
exactly; CBFS holds a flat list of files, so there is no path to give.

Note that the command reads the load address without checking it, as cbfsinit
does, so a value which is not a hexadecimal number is taken as zero rather
than reported.

Example
-------

This reads from the small CBFS built by hand in the example on the
:doc:`cbfsinit<cbfsinit>` page, which holds a 16-byte raw file called hello and
a 32-byte payload called u-boot::

    => cbfsload 2000000 hello
    reading hello

    16 bytes read
    => md.b 2000000 10
    02000000: 78 56 34 12 78 56 34 12 78 56 34 12 78 56 34 12  xV4.xV4.xV4.xV4.
    => printenv filesize
    filesize=10

A byte count stops the read short, leaving the rest of the buffer alone::

    => cbfsload 2000000 u-boot 8
    reading u-boot

    8 bytes read
    => md.b 2000000 10
    02000000: dd cc bb aa dd cc bb aa 00 00 00 00 00 00 00 00  ................

A name which is not in the archive is reported, as is a command line with too
few arguments::

    => cbfsload 2000000 nope
    File not found: nope
    => cbfsload 2000000
    usage: cbfsload <addr> <filename> [bytes]

Configuration
-------------

The command is only available if CONFIG_CMD_CBFS=y, which needs
CONFIG_FS_CBFS=y.

Return value
------------

The return value $? is 0 (true) if the file is read. It is 1 (false) if no CBFS
has been read, if the archive holds no file of that name, or if fewer than two
arguments are given, in which case the command prints a usage line of its own
rather than the full help text.

See also
--------

* :doc:`cbfsinit<cbfsinit>` for reading the CBFS this command loads from
* :doc:`cbfsls<cbfsls>` for finding the name of a file to load
* :doc:`cbfsinfo<cbfsinfo>` for showing the header of the archive
* :doc:`load<load>` which reads the same files, with 'cbfs' as the interface,
  and reports the time the read took
