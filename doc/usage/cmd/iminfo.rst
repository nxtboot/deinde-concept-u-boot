.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: iminfo (command)

iminfo command
==============

Synopsis
--------

::

    iminfo [<addr> ...]

Description
-----------

The iminfo command prints the header of an image held in memory and checks
that the image is intact. It is what to reach for after loading an image and
before booting it, since it says what the image claims to be and whether it
survived the journey.

The image format is worked out from the data itself, so the same command
handles a legacy uImage, a FIT image and an Android boot image. Which of those
are recognised depends on the build; an image in a format the build does not
know about is reported as 'Unknown image format!'.

For a legacy uImage the magic number, the header checksum and the checksum of
the payload are all verified, and the fields of the header are printed. For a
FIT image the format is checked and every hash in it is verified.

addr
    address of the image, hexadecimal. More than one may be given, in which
    case each is checked in turn and the results are printed one after
    another. Without any address the value of the *loadaddr* environment
    variable is used, which starts out as CONFIG_SYS_LOAD_ADDR

Example
-------

Checking an image which is intact::

    => iminfo 1000000

    ## Checking Image at 01000000 ...
       Legacy image found
       Image Name:   test image
       Created:      1970-01-01   0:00:00 UTC
       Image Type:   Sandbox Linux Kernel Image (uncompressed)
       Data Size:    64 Bytes = 64 Bytes
       Load Address: 12345678
       Entry Point:  1234567c
       Verifying Checksum ... OK

The header is printed before the payload is checked, so a single damaged byte
in the payload still shows all of the fields, followed by the failure::

    => mw.b 1000045 ff
    => iminfo 1000000

    ## Checking Image at 01000000 ...
       Legacy image found
       Image Name:   test image
       Created:      1970-01-01   0:00:00 UTC
       Image Type:   Sandbox Linux Kernel Image (uncompressed)
       Data Size:    64 Bytes = 64 Bytes
       Load Address: 12345678
       Entry Point:  1234567c
       Verifying Checksum ...    Bad Data CRC
    => echo $?
    1

A FIT is recognised in the same way. Each image within it is listed and the
hash of every image is checked::

    => iminfo 1000000

    ## Checking Image at 01000000 ...
       FIT image found
       FIT description: test FIT
       Created:         1970-01-01   0:00:00 UTC
        Image 0 (kernel)
         Description:   test kernel
         Created:       1970-01-01   0:00:00 UTC
         Type:          Kernel Image
         Compression:   uncompressed
         Data Start:    0x010000b4
         Data Size:     64 Bytes = 64 Bytes
         Architecture:  Sandbox
         OS:            Linux
         Load Address:  0x12345678
         Entry Point:   0x1234567c
         Hash algo:     crc32
         Hash value:    100ece8c
    ## Checking hash(es) for FIT Image at 01000000 ...
       Hash(es) for Image 0 (kernel): crc32+

Memory which holds no image at all is reported without any of the fields::

    => iminfo 2000000

    ## Checking Image at 02000000 ...
    Unknown image format!

Configuration
-------------

The iminfo command is available if CONFIG_CMD_IMI=y, which is the default.

The legacy uImage format is understood if CONFIG_LEGACY_IMAGE_FORMAT=y, FIT
images if CONFIG_FIT=y and Android boot images if
CONFIG_ANDROID_BOOT_IMAGE=y. The creation time is printed only if
CONFIG_TIMESTAMP=y or CONFIG_CMD_DATE=y.

Return value
------------

The return value $? is 0 (true) if every image given checks out. It is 1
(false) if any of them does not, whether because the format is not recognised,
a checksum does not match, or the FIT image has a bad hash. A failure part-way
along does not stop the remaining addresses being checked.

See also
--------

* :doc:`bootm<bootm>` for booting an image whose header this command prints
* :doc:`load<load>` for reading an image into memory in the first place
* :doc:`imxtract<imxtract>` for pulling a single component out of an image
* *imls* for listing the images stored in flash
