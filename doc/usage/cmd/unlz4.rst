.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: unlz4 (command)

unlz4 command
=============

Synopsis
--------

::

    unlz4 <srcaddr> <dstaddr> <dstsize>

Description
-----------

The unlz4 command decompresses an lz4 frame held in memory, writing the result
somewhere else in memory. It is the counterpart of the compression done on the
host with the *lz4* tool, so a file compressed there can be read into memory
with :doc:`load<load>` and expanded in place of a larger one.

U-Boot decompresses images by itself when booting them, so this command is
mostly useful for data which is not part of a boot image, or for checking that
a frame is intact before it is used.

Only a single, standard frame is handled. The frame must say that its blocks
are independent, which is what the *lz4* tool produces by default. The header
checksum is not verified and the content checksum at the end of the frame is
ignored, so a truncated or corrupted frame is only noticed when the
decompression itself fails.

srcaddr
    address of the lz4 frame, hexadecimal

dstaddr
    address to write the uncompressed data to, hexadecimal

dstsize
    room available at *dstaddr*, in bytes, hexadecimal. Decompression stops
    when this is used up, so the value must be at least the uncompressed size;
    otherwise nothing useful is produced. It is not a guess at the source size

On success the uncompressed size is printed and left in the *filesize*
environment variable, so the next command can pick it up without it being
worked out by hand.

Example
-------

This reads a compressed file into memory and expands it::

    => mount hostfs - /host
    => load 1000000 /host/plain.lz4
    276 bytes read
    => unlz4 1000000 2000000 1000
    Uncompressed size: 350 = 0x15E
    => md.b 2000000 30
    02000000: 49 20 61 6d 20 61 20 68 69 67 68 6c 79 20 63 6f  I am a highly co
    02000010: 6d 70 72 65 73 73 61 62 6c 65 20 62 69 74 20 6f  mpressable bit o
    02000020: 66 20 74 65 78 74 2e 0a 49 20 61 6d 20 61 20 68  f text..I am a h
    => printenv filesize
    filesize=15e

A destination too small for the result stops the decompression part-way::

    => unlz4 1000000 2000000 40
    Uncompressed err :-71

Something which is not an lz4 frame is refused by the header check::

    => unlz4 1000000 2000000 1000
    Uncompressed err :-93

The number after *err* is a negative error code: -71 (EPROTO) means the data
did not decompress, -93 (EPROTONOSUPPORT) that the magic number or version is
wrong or the blocks are not independent, and -22 (EINVAL) that a reserved bit
is set in the frame header.

Configuration
-------------

The unlz4 command is available if CONFIG_CMD_UNLZ4=y. It is enabled by default
where CONFIG_CMD_BOOTI=y.

Return value
------------

The return value $? is 0 (true) if the frame is decompressed. It is 1 (false)
if the decompression fails, or if the command is not given exactly three
arguments.

See also
--------

* :doc:`lzmadec<lzmadec>` for the same job on an LZMA stream
* :doc:`load<load>` for reading a compressed file into memory first
* :doc:`md<md>` for looking at the result
* :doc:`bootm<bootm>` for booting an image, which decompresses it as a step
* *unzip* for decompressing a gzip-compressed region
* *zip* for compressing one
