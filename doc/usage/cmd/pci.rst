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
