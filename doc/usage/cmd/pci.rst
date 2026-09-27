.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: pci (command)

pci command
===========

Synopsis
--------

::

    pci [<bus> | *] [long]
    pci enum
    pci header <b.d.f>
    pci bar <b.d.f>
    pci intr <b.d.f>
    pci regions [<bus> | *]
    pci display[.b|.w|.l] <b.d.f> [<addr> [<count>]]
    pci next[.b|.w|.l] <b.d.f> <addr>
    pci modify[.b|.w|.l] <b.d.f> <addr>
    pci write[.b|.w|.l] <b.d.f> <addr> <value>

Description
-----------

The pci command lists PCI devices and accesses their configuration space.
Devices are given as bus.device.function in hex, e.g. 0.1f.0.

pci [<bus> | \*] [long]
    List the devices on a bus, or on every bus when no bus or '*' is given.
    Every bus is probed first, including those behind bridges, then the
    buses are shown in order of their numbers, which need not be
    contiguous. The long form shows each device's header.

pci enum
    Enumerate the PCI buses, probing every device.

pci header
    Show a device's configuration header.

pci bar
    Show a device's BARs, with their base and size.

pci intr
    Show how a device signals interrupts: its legacy interrupt pin (A to D,
    or '-' for none) and line, its MSI capability (whether it is enabled,
    the number of vectors enabled and supported, and the message address and
    data) and its MSI-X capability (whether it is enabled or masked, the
    table size and where the table and pending-bit array are, as a BAR and
    offset). The MSI-X table entries which have an address are then listed,
    with 'masked' for those which are masked. This is useful when a device's
    interrupts go missing, to see what it has been told to send.

pci regions
    Show the PCI regions of a bus's controller, or of every controller when
    no bus or '*' is given.

pci display, next, modify, write
    Read or write the configuration space, as bytes, words or longs (the
    default).

Configuration
-------------

The pci command is available when CONFIG_CMD_PCI is enabled.

Example
-------

Listing every bus on sandbox, where pci4 is bus 0x10 with a bridge to bus
0x11 and the bridge on pci2 leads to bus 0x20::

    => pci
    BusDevFun  VendorId   DeviceId   Device Class       Sub-Class
    _____________________________________________________________
    00.00.00   0x1234     0x5678     Simple comm. controller 0x00
    ...
    03.00.00   0x1234     0x5678     Simple comm. controller 0x00
    10.00.00   0x1234     0x5675     Bridge device           0x04
    11.00.00   0x1234     0x5678     Simple comm. controller 0x00
    20.00.00   0x1234     0x5678     Simple comm. controller 0x00

A device with MSI-X but no legacy interrupt, on sandbox::

    => pci intr 0.1f.0
    INTx: pin - line 255
    MSI-X: disabled, 1 entries, table BAR1+0, PBA BAR1+800

An xHCI controller on an AMD EPYC board, which has both MSI and MSI-X::

    => pci intr 4a.0.4
    INTx: pin C line 0
    MSI:  disabled, 1 of 8 vectors, address 0000000000000000 data 0000
    MSI-X: disabled, 8 entries, table BAR0+fe000, PBA BAR0+ff000
