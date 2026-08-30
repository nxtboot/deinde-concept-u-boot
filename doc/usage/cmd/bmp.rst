.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: bmp (command)

bmp command
===========

Synopsis
--------

::

    bmp info [<imageAddr>]
    bmp display [<imageAddr> [<x> <y>]]

Description
-----------

The bmp command looks at a BMP image which is already in memory and shows it on
a display. BMP is the bitmap format defined by Microsoft; its header says how
large the image is, how many bits each pixel takes and how the pixels are
encoded.

The image must be loaded into memory first, with :doc:`load<load>` or any other
command which reads a file. Where CONFIG_VIDEO_BMP_GZIP=y the image may be
gzip-compressed, in which case it is decompressed into a temporary buffer before
it is used.

bmp info
~~~~~~~~

Show the size of the image in pixels, the number of bits per pixel and the
compression method recorded in its header. Compression 0 means the pixels are
stored as they are; 1 and 2 are the run-length encodings BMP defines for 8- and
4-bit images.

bmp display
~~~~~~~~~~~

Draw the image on the first video device. Only the part of the image which fits
on the display is drawn, so an image larger than the display is cut off at the
right and the bottom.

imageAddr
    address of the image, hexadecimal. Defaults to the address in $loadaddr

x, y
    position of the top-left corner of the image, decimal, counted from the top
    left of the display. Both default to the position in $splashpos, which is
    0, 0 when that is not set. The letter 'm' centres the image on that axis

Example
-------

Sandbox can read a file straight from the host filesystem, so an image needs no
disk::

    => load hostfs - 1000000 tools/logos/denx.bmp
    15538 bytes read in 1 ms (14.8 MiB/s)
    => bmp info 1000000
    Image size    : 160 x 96
    Bits per pixel: 8
    Compression   : 0

Drawing produces no output. The image goes in the top-left corner unless a
position is given, and 'm' centres it::

    => bmp display 1000000
    => bmp display 1000000 20 10
    => bmp display 1000000 m m

Memory which holds no image is reported::

    => bmp info 2000000
    There is no valid bmp file at the given address

A position off the display prints nothing and fails::

    => bmp display 1000000 2000 2000
    => echo $?
    1

Supported images
----------------

The image must be a Windows BMP file: the two-byte ``BM`` signature, the
14-byte file header, then a device-independent bitmap header. Only the width,
height, bit count and compression are read out of that header, and its size
field is used to find the palette which follows it, so a BITMAPINFOHEADER and
the longer V4 and V5 headers all work. The 12-byte OS/2 core header is not
supported.

Depths of 1 and 8 bits per pixel are always understood, each taking its colours
from the palette in the file. The others are optional:

======  ====================
Depth   Option
======  ====================
16 bpp  CONFIG_BMP_16BPP=y
24 bpp  CONFIG_BMP_24BPP=y
32 bpp  CONFIG_BMP_32BPP=y
======  ====================

An 8-bit image may be RLE8-compressed if CONFIG_VIDEO_BMP_RLE8=y; RLE4 is not
handled. A gzip-compressed file is unpacked first if CONFIG_VIDEO_BMP_GZIP=y.

The display itself must be 1, 8, 16 or 32 bits per pixel and its depth has to
suit the image, otherwise the command reports the two depths and stops.

Creating an image
-----------------

ImageMagick writes files which this command reads. Asking for a palette gives
an 8-bit image::

    convert in.png -type Palette -colors 256 BMP3:out.bmp

ImageMagick compresses an 8-bit BMP with RLE8 unless it is told not to, so
``-compress RLE`` adds nothing and ``-compress None`` is what avoids it::

    convert in.png -type Palette -colors 256 -compress None BMP3:out.bmp

Without the palette options the file is 24 bits per pixel::

    convert in.png BMP3:out.bmp

The ``BMP3:`` prefix asks for the 40-byte BITMAPINFOHEADER. Leaving it off
writes the 124-byte V5 header instead, which this command also reads.

An RLE4 file cannot be written this way, which suits U-Boot since it does not
read one: asking for 16 colours still produces 8-bit RLE8.

Configuration
-------------

The bmp command is available if CONFIG_CMD_BMP=y. It needs a video device, so
the option depends on CONFIG_VIDEO=y.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) if
there is no BMP image at the address, if there is no video device, if the
position is outside the display or if the display cannot show an image of that
depth.

A missing or extra argument is reported as a usage error, which is also 1
(false).

See also
--------

* :doc:`load<load>` for reading the image into memory
* :doc:`video<video>` for the display the image is drawn on
* :doc:`cls<cls>` for clearing the display before drawing an image on it
* the `BMP file format`_, which Microsoft documents

.. _`BMP file format`: https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage
