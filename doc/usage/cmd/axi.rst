.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: axi (command)

axi command
===========

Synopsis
--------

::

    axi bus [<bus>]
    axi dev [<bus>]
    axi md <size> <address> [<count>]
    axi mw <size> <address> <value> [<count>]

Description
-----------

The axi command reads and writes an AXI bus. AXI (Advanced eXtensible
Interface) is an on-chip interconnect used to reach functional blocks in an SoC
or IP cores in an FPGA. It is a separate address space from memory, reached
through a device driver rather than through a load or store, so :doc:`md<md>`
cannot show it.

The commands operate on the currently selected bus, which is chosen with
'axi dev' and remembered until it is changed.

axi bus
~~~~~~~

Show the AXI buses. Each is listed by its sequence number and name, marked
'(active)' if it is probed, with its children indented below it. With a bus
number, show only that bus.

axi dev
~~~~~~~

Show the currently selected bus, or select the bus to use for later commands.
Before anything is selected the current bus is reported as -1 and 'axi md' and
'axi mw' refuse to run.

axi md
~~~~~~

Display data read from the bus, in the same layout as :doc:`md<md>`: the
address, the values in hex, then an ASCII rendering of the bytes read.

axi mw
~~~~~~

Write a value to the bus, repeating it *count* times at consecutive addresses.

size
    width of each access, in bits: 8, 16 or 32. A driver need not support all
    three; one which does not fails the access rather than splitting it up.

address
    address on the bus, hexadecimal

count
    number of values to read or write, hexadecimal. It counts values, not
    bytes, so 'axi md 8 0 10' shows 16 bytes and 'axi md 32 0 10' shows 64.
    For 'axi md' it defaults to 40 (0d64) and for 'axi mw' to 1.

value
    value to write, hexadecimal

The size, address and count given to 'axi md' are remembered. Pressing Enter on
an empty command line repeats the command, showing the next block of the bus in
the same format, as :doc:`md<md>` does.

Example
-------

Sandbox provides an emulated AXI bus with a 1KB store on it::

    => axi bus
    Bus 0:	axi@0  (active)
      store@0
    => axi dev 0
    Setting bus to 0
    => axi dev
    Current bus is 0

The store keeps whatever is written to it::

    => axi mw 32 0 deadbeef
    => axi md 32 0 4
    00000000: deadbeef 00000000 00000000 00000000  ................

The same memory read as bytes. The sandbox store emulates a big-endian device,
so a 32-bit write appears in that order::

    => axi md 8 0 10
    00000000: de ad be ef 00 00 00 00 00 00 00 00 00 00 00 00  ................

A count fills consecutive units of the chosen size::

    => axi mw 8 40 aa 4
    => axi md 8 40 8
    00000040: aa aa aa aa 00 00 00 00                          ........

Configuration
-------------

The axi command is available if CONFIG_CMD_AXI=y.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) if
no bus is selected, if the bus number given to 'axi bus' does not exist, or if
the driver rejects the access.

An unknown size, or a missing argument, is reported as a usage error, which is
also 1 (false).

See also
--------

* :doc:`md<md>` for displaying memory rather than an AXI bus
* :doc:`iod<iod>`, :doc:`iow<iow>` for reading and writing I/O space, another
  address space reached through accessors
