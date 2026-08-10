.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: mdc (command)

mdc command
===========

Synopsis
--------

::

    mdc [.b, .w, .l, .q] <address> <count> <delay>

Description
-----------

The mdc command displays memory over and over again, waiting for a delay between
each display. It is useful for watching a value which changes on its own, such
as a hardware register or a buffer which is being filled in by a device.

The command does not stop by itself. Press Ctrl-C to end it, which prints
'Abort'. Note that Ctrl-C is only noticed between displays, so a long delay
means a wait before the command responds. The display itself watches for Ctrl-C
too, using it to cut the current dump short, so a press which lands part way
through a display only shortens it; press Ctrl-C again to end the command.

Each display is the same as that produced by the :doc:`md` command.

address
    start address to display, hexadecimal. The offset set by the :doc:`base`
    command is added to it

count
    number of values to display, hexadecimal. Note that this is not the same as
    the number of bytes, unless .b is used

delay
    time to wait between displays, in milliseconds, decimal

The data_size suffix sets the size of each value (defaults to .l):

    =========  ===================
    data_size  Output size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    .q         quadword (64 bits)
    =========  ===================

Example
-------

This displays four bytes twice a second until Ctrl-C is pressed::

    => mw.b 40000 aa 4
    => mdc.b 40000 4 500
    00040000: aa aa aa aa                                      ....
    00040000: aa aa aa aa                                      ....
    Abort

Configuration
-------------

The mdc command is available if CONFIG_CMD_MX_CYCLIC=y. Support for 64-bit
values (mdc.q) is only available on 64-bit targets.

Return value
------------

The return value $? is 0 (true) when the command is stopped with Ctrl-C. It is
1 (false) if any of the three arguments is missing.
