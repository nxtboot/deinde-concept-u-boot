.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: iod (command)

iod command
===========

Synopsis
--------

::

    iod[<data_size>] <address> [<length>]

Description
-----------

The iod command displays the contents of I/O space, in the same layout as the
:doc:`md<md>` command: the address, the values in hex, then an ASCII rendering
of the bytes read.

I/O space is a separate address space from memory. It is reached with the
inb(), inw() and inl() accessors rather than with a load, so it cannot be shown
by :doc:`md<md>`. On x86 it is the port space addressed by the 'in' and 'out'
instructions; other architectures may map it onto a region of memory or not
provide one at all.

Each value is read with a single access of the chosen size, so a device
register which must be read as a whole can be displayed by asking for the size
it expects.

address
    start address to display, hexadecimal

data_size
    size of each value to read and display (defaults to .l):

    =========  ===================
    data_size  Output size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    =========  ===================

There is no .q size: I/O space is limited to 32-bit transfers, since the in and
out instructions used to reach it have no 64-bit form. Asking for .q fails,
rather than falling back to a smaller access.

length
    number of values to display, hexadecimal. Defaults to 40 (0d64). Note that
    this is not the same as the number of bytes, unless .b is used.

The address, size and length are remembered. Pressing Enter on an empty command
line repeats the command, showing the next block of I/O space in the same
format, as :doc:`md<md>` does.

Example
-------

On sandbox the I/O space is provided by the emulated PCI devices, so the bus
must be enumerated before there is anything to read. The device at 0.0.0 has a
one-byte I/O region which returns the value last written to it::

    => pci enum
    => iod.b 20000000 1
    20000000: 00                                               .
    => iow.b 20000000 2
    => iod.b 20000000 1
    20000000: 02                                               .

A wider access reads the same region in one go. The emulator only implements
the first byte, so the rest reads back as all-ones, which is what a PCI bus
returns when nothing responds::

    => iod 20000000 1
    20000000: ffffff02                             ....

Configuration
-------------

The iod command is available if CONFIG_CMD_IO=y.

Return value
------------

The return value $? is 0 (true) if the values are displayed. It is 1 (false) if
no address is given, or if the data_size is not one of .b, .w and .l.

See also
--------

* *iow* for writing a value to I/O space
* :doc:`md<md>` for displaying memory rather than I/O space
* :doc:`mm<mm>`, :doc:`nm<nm>` for reading and modifying memory interactively
