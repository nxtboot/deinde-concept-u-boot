.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: random (command)

random command
==============

Synopsis
--------

::

    random <address> <len> [<seed>]

Description
-----------

The random command fills a region of memory with pseudo-random data. It is
useful for preparing a buffer whose contents are known but not uniform, for
example to check that a driver moves every byte of a transfer, or to give
*crc32* something to work on which a run of identical bytes would not
exercise.

The data comes from U-Boot's own pseudo-random number generator, which is a
small algorithm meant for testing. It is not suitable for anything which needs
unpredictable numbers, such as keys or nonces; use the :doc:`rng` command for
that, as it reads a hardware random number generator.

Giving a seed makes the data repeatable: the same seed always produces the same
bytes, so a region can be filled, used, and then filled again to compare
against. Without a seed the command picks one from the timer, and the data
differs from one run to the next.

address
    address to fill, hexadecimal. Note that, unlike *mw*, the offset set by the
    :doc:`base` command is *not* added to it

len
    number of **bytes** to fill, hexadecimal. Unlike the count taken by
    :doc:`md` and most other memory commands, this is a byte count, not a number
    of values

seed
    seed for the random number generator, hexadecimal. A seed of 0 is refused,
    since it would leave the generator stuck, and 0xdeadbeef is used instead
    along with a message saying so

Example
-------

This fills 0x12 bytes with data from a fixed seed. The byte after the region is
left alone::

    => mw.b 40000 0 20
    => random 40000 12 1234
    18 bytes filled with random data
    => md.b 40000 20
    00040000: f7 f1 94 4a e2 0f e5 41 2e c2 3e e3 f3 ed 18 54  ...J...A..>....T
    00040010: 20 2b 00 00 00 00 00 00 00 00 00 00 00 00 00 00   +..............

Note that the count in the message is in decimal, while len is in hexadecimal.

Repeating the command with the same seed writes the same bytes again::

    => random 40000 12 1234
    18 bytes filled with random data
    => md.b 40000 4
    00040000: f7 f1 94 4a                                      ...J

A seed of 0 is replaced::

    => random 40000 12 0
    The seed cannot be 0. Using 0xDEADBEEF.
    18 bytes filled with random data

Configuration
-------------

The random command is available if CONFIG_CMD_RANDOM=y.

Return value
------------

The return value $? is 0 (true) if the region is filled. It is 1 (false) if the
address or the length is missing, or if more than three arguments are given.

See also
--------

* *mw* for filling a region with a constant value
* :doc:`md<md>` for displaying the result
* :doc:`rng<rng>` for reading a hardware random number generator
* :doc:`mtest<mtest>` for checking that memory holds what is written to it
