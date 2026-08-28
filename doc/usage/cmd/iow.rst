.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: iow (command)

iow command
===========

Synopsis
--------

::

    iow[<data_size>] <address> <value>

Description
-----------

The iow command writes a single value to I/O space.

I/O space is a separate address space from memory. It is reached with the
outb(), outw() and outl() accessors rather than with a store, so it cannot be
written by the mw command. On x86 it is the port space addressed by the 'in'
and 'out' instructions; other architectures may map it onto a region of memory
or not provide one at all.

The value is written with a single access of the chosen size. This matters for
a device register which reacts to the write, since a register expecting a
32-bit write generally does not accept four byte writes instead.

Unlike the mw command, iow writes one value only; there is no count argument
and no way to fill a region.

address
    address to write to, hexadecimal

value
    value to write, hexadecimal. It is truncated to the data_size

data_size
    size of the write (defaults to .l):

    =========  ===================
    data_size  Value size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    =========  ===================

There is no .q size: I/O space is limited to 32-bit transfers, since the in and
out instructions used to reach it have no 64-bit form. Asking for .q fails,
rather than falling back to a smaller access.

Example
-------

On sandbox the I/O space is provided by the emulated PCI devices, so the bus
must be enumerated before there is anything to write to. The device at 0.0.0
has a one-byte I/O region which hands back the value last written to it::

    => pci enum
    => iow.b 20000000 2
    => iod.b 20000000 1
    20000000: 02                                               .
    => iow.b 20000000 55
    => iod.b 20000000 1
    20000000: 55                                               U

Configuration
-------------

The iow command is available if CONFIG_CMD_IO=y.

Return value
------------

The return value $? is 0 (true) if the write is done. It is 1 (false) if the
address or the value is missing, or if the data_size is not one of .b, .w and
.l.

Note that nothing is reported when no device answers at that address: an I/O
write is not acknowledged, so the only way to tell is to read the value back.

See also
--------

* :doc:`iod<iod>` for displaying I/O space
* *mw* for writing a value to memory rather than to I/O space
* :doc:`mm<mm>`, :doc:`nm<nm>` for modifying memory interactively
* :doc:`axi<axi>` for writing to an AXI bus, another separate address space
