.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: lzmadec (command)

lzmadec command
===============

Synopsis
--------

::

    lzmadec <srcaddr> <dstaddr> [<dstsize>]

Description
-----------

The lzmadec command decompresses an LZMA stream held in memory, writing the
result somewhere else in memory. The stream is the plain 'LZMA alone' format
written by the *lzma* tool on the host: five bytes of properties, the
uncompressed size as a 64-bit value, then the data. It is not the *xz*
container, which this command cannot read.

U-Boot decompresses images by itself when booting them, so this command is
mostly useful for data which is not part of a boot image, or for checking that
a stream is intact before it is used.

srcaddr
    address of the LZMA stream, hexadecimal

dstaddr
    address to write the uncompressed data to, hexadecimal

dstsize
    room available at *dstaddr*, in bytes, hexadecimal. Decompression stops
    when this is used up, so a value below the uncompressed size leaves only
    the first part of the data behind and the command fails. Without it there
    is no limit and whatever the stream holds is written, however large

On success the uncompressed size is printed and left in the *filesize*
environment variable, so the next command can pick it up without it being
worked out by hand.

A stream need not record its uncompressed size, and one written to a pipe ends
with a marker instead, which is why *dstsize* is worth giving: for such a
stream it is the only thing stopping a corrupt or hostile one from writing over
whatever follows the destination.

Example
-------

This reads a compressed file into memory and expands it::

    => mount hostfs - /host
    => load 1000000 /host/plain.lzma
    229 bytes read
    => lzmadec 1000000 2000000
    Uncompressed size: 350 = 0X15E
    => md.b 2000000 30
    02000000: 49 20 61 6d 20 61 20 68 69 67 68 6c 79 20 63 6f  I am a highly co
    02000010: 6d 70 72 65 73 73 61 62 6c 65 20 62 69 74 20 6f  mpressable bit o
    02000020: 66 20 74 65 78 74 2e 0a 49 20 61 6d 20 61 20 68  f text..I am a h
    => printenv filesize
    filesize=15e

A destination too small for the result gives no message at all, so the return
value is the only sign that anything went wrong::

    => lzmadec 1000000 3000000 40
    => echo $?
    1

The same silence follows a stream which does not decompress, so this command
says less about a failure than :doc:`unlz4<unlz4>` does.

Configuration
-------------

The lzmadec command is available if CONFIG_CMD_LZMADEC=y. It is enabled by
default where CONFIG_CMD_BOOTI=y.

Return value
------------

The return value $? is 0 (true) if the stream is decompressed. It is 1 (false)
if the decompression fails, including when the destination is too small, or if
the command is given fewer than two or more than three arguments.

See also
--------

* :doc:`unlz4<unlz4>` for the same job on an lz4 frame
* :doc:`load<load>` for reading a compressed file into memory first
* :doc:`md<md>` for looking at the result
* :doc:`bootm<bootm>` for booting an image, which decompresses it as a step
* *unzip* for decompressing a gzip-compressed region
