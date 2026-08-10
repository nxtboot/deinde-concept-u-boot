.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: mwc (command)

mwc command
===========

Synopsis
--------

::

    mwc [.b, .w, .l, .q] <address> <value> <delay>

Description
-----------

The mwc command writes a value to memory over and over again, waiting for a
delay between each write. It is useful for poking a hardware register at a
steady rate, for example to keep a watchdog or a bus device alive while
something else is examined.

Only a single value is written each time, whatever the data_size, so this is not
a way to fill a region; use the mw command for that.

The command does not stop by itself. Press Ctrl-C to end it, which prints
'Abort'. Note that Ctrl-C is only noticed between writes, so a long delay means
a wait before the command responds.

address
    address to write to, hexadecimal. The offset set by the :doc:`base` command
    is added to it

value
    value to write, hexadecimal

delay
    time to wait between writes, in milliseconds, decimal

The data_size suffix sets the size of the value written (defaults to .l):

    =========  ===================
    data_size  Value size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    .q         quadword (64 bits)
    =========  ===================

Example
-------

This writes a byte twice a second until Ctrl-C is pressed. Only the first byte
is touched::

    => mw.b 50000 0 4
    => md.b 50000 4
    00050000: 00 00 00 00                                      ....
    => mwc.b 50000 55 500
    Abort
    => md.b 50000 4
    00050000: 55 00 00 00                                      U...

Configuration
-------------

The mwc command is available if CONFIG_CMD_MX_CYCLIC=y. Support for 64-bit
values (mwc.q) is only available on 64-bit targets.

Return value
------------

The return value $? is 0 (true) when the command is stopped with Ctrl-C. It is
1 (false) if any of the three arguments is missing.

See also
--------

* :doc:`mdc<mdc>` for displaying memory repeatedly
* *mw* for writing a value to memory once, or filling a region
* :doc:`mm<mm>`, :doc:`nm<nm>` for modifying memory interactively
* :doc:`base<base>` for the address offset applied to these commands
