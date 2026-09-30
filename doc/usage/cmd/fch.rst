.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fch (command)

fch command
===========

Synopsis
--------

::

    fch irq [-a]
    fch pm

Description
-----------

The fch command shows the state of the Fusion Controller Hub (FCH), the
southbridge of an AMD EPYC Turin processor.

irq
    Show the FCH's interrupt routing, written through the index/data pair at
    I/O ports 0xc00/0xc01. Each source (index) has an IRQ for PIC mode and
    one for I/O APIC mode. Sources which are not routed are not shown unless
    -a is given. Indexes 0x08 (Misc) and 0x09 (Misc0) hold settings rather
    than IRQs.

pm
    Dump the FCH's power-management registers (PMx00-PMxff), which control
    among other things the decoding of the I/O APIC, the HPET and the ACPI
    blocks, and how interrupts are delivered.

Configuration
-------------

The fch command is available on AMD EPYC Turin.

Example
-------

::

    => fch irq
    Index  PIC  APIC
       00   0a    10
       01   0b    11
    ...
       09   91    00
       10   09    09
    ...
    => fch pm
    fed80300: e3060b77 00000402 021c0514 55432106  w............!CU
    ...
