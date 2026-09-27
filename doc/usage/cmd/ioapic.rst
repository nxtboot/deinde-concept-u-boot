.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ioapic (command)

ioapic command
==============

Synopsis
--------

::

    ioapic [-a] [<addr>]

Description
-----------

The ioapic command shows an I/O APIC's ID, version and redirection table,
which says where each of its interrupt pins is delivered: the vector, the
destination APIC ID, the delivery mode, the polarity and trigger mode, and
whether the pin is masked. It is useful for checking whether a legacy
interrupt, such as the timer or a serial port, is routed.

-a
    Show every pin. By default masked pins are not shown.

addr
    Address of the I/O APIC, in hex. The default is 0xfec00000, where the
    chipset's I/O APIC normally sits. Some platforms have others, such as
    one for each PCIe root complex on AMD EPYC; these may only respond once
    the root complex is set up, e.g. after 'pci enum'.

Configuration
-------------

The ioapic command is available on x86 when CONFIG_APIC is enabled.

Example
-------

On QEMU, where U-Boot leaves every pin masked::

    => ioapic
    I/O APIC at fec00000: ID 0, version 20, 24 entries
    Pin  Vector  Dest  Delivery  Polarity  Trigger  Mask
    => ioapic -a
    I/O APIC at fec00000: ID 0, version 20, 24 entries
    Pin  Vector  Dest  Delivery  Polarity  Trigger  Mask
      0  00      00    fixed         high      edge     masked
      1  00      00    fixed         high      edge     masked
    ...
     23  00      00    fixed         high      edge     masked
