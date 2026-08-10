.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: nm (command)

nm command
==========

Synopsis
--------

::

    nm [.b, .w, .l, .q] <address>

Description
-----------

The nm command modifies memory interactively at a constant address. It shows the
address and the value held there, then waits for a new value to be typed. Unlike
:doc:`mm` it stays at the same address, which suits a hardware register where
writing several values in turn is useful.

The response to the prompt decides what happens:

value
    a hexadecimal value is written to the address shown and the address is shown
    again with its new value

<Enter>
    nothing is written and the address is shown again

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
line repeats the command at the same address.

Example
-------

This writes two values in turn to the same address::

    => mw 20000 0 4
    => nm 20000
    00020000: 00000000 ? abcd
    00020000: 0000abcd ? 1111
    00020000: 00001111 ? q
    => md 20000 4
    00020000: 00001111 00000000 00000000 00000000  ................

Configuration
-------------

The nm command is available if CONFIG_CMD_MEMORY=y. Support for 64-bit values
(nm.q) is only available on 64-bit targets.

Return value
------------

The return value $? is 0 (true) if the command completes. It is 1 (false) if the
address is missing, if more than one address is given, or if the data_size is
not valid.
