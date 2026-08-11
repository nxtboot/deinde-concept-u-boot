.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: mm (command)

mm command
==========

Synopsis
--------

::

    mm [.b, .w, .l, .q] <address>

Description
-----------

The mm command modifies memory interactively, one value at a time. It shows the
address and the value held there, then waits for a new value to be typed. Once
the value is dealt with it moves on to the next address, so a block of memory
can be filled in without typing an address each time.

The response to the prompt decides what happens:

value
    a hexadecimal value is written to the address shown and the command moves on
    to the next address

<Enter>
    the address is left as it is and the command moves on to the next address

\-
    the address is left as it is and the command moves back to the previous
    address

anything else
    the command ends. Any text which does not start with a hexadecimal digit
    will do, so 'q' is a convenient way to finish

The address is added to the offset set by the :doc:`base` command.

address
    address to modify, hexadecimal

The data_size suffix sets the size of each value (defaults to .l):

    =========  ===================
    data_size  Value size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    .q         quadword (64 bits)
    =========  ===================

The address and data_size are remembered, so pressing Enter on an empty command
line repeats the command from where it left off.

Use the nm command to modify the same address repeatedly, rather than moving on.

Example
-------

This modifies the first two words of a block of memory, then checks the result::

    => mw 10000 0 8
    => md 10000 8
    00010000: 00000000 00000000 00000000 00000000  ................
    00010010: 00000000 00000000 00000000 00000000  ................
    => mm 10000
    00010000: 00000000 ? 1234
    00010004: 00000000 ? 5678
    00010008: 00000000 ? q
    => md 10000 8
    00010000: 00001234 00005678 00000000 00000000  4...xV..........
    00010010: 00000000 00000000 00000000 00000000  ................

This works a byte at a time and uses '-' to go back and correct a value::

    => mw.b 30000 0 4
    => mm.b 30000
    00030000: 00 ? 11
    00030001: 00 ? 22
    00030002: 00 ? -
    00030001: 22 ? 33
    00030002: 00 ? q
    => md.b 30000 4
    00030000: 11 33 00 00                                      .3..

Configuration
-------------

The mm command is available if CONFIG_CMD_MEMORY=y. Support for 64-bit values
(mm.q) is only available on 64-bit targets.

Return value
------------

The return value $? is 0 (true) if the command completes. It is 1 (false) if the
address is missing, if more than one address is given, or if the data_size is
not valid.

See also
--------

* :doc:`nm<nm>` for modifying memory at a constant address
* :doc:`mdc<mdc>` for displaying memory repeatedly
* :doc:`md<md>` for displaying memory once
* *mw* for writing a value to memory without prompting
* :doc:`base<base>` for the address offset applied to these commands
