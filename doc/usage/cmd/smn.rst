.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: smn (command)

smn command
===========

Synopsis
--------

::

    smn md <addr> [<count> [<bus>]]
    smn mw <addr> <value> [<bus>]

Description
-----------

The smn command reads and writes registers on the System Management Network
(SMN) of an AMD EPYC Turin processor. Most of the processor's internal
blocks, such as the IOHC, NBIF, data fabric and SMU, have their registers
only in this space, which is reached through an index/data pair in the
configuration space of a root complex.

md
    Read <count> 32-bit words (default 1) starting at <addr>, both in hex.

mw
    Write a 32-bit word to <addr>.

bus
    The root bus whose index/data pair to use, in hex. The default is 0. The
    same space is reached through any root bus.

Configuration
-------------

The smn command is available on AMD EPYC Turin.

Example
-------

Reading the I/O APIC base register of the root complex for bus 0, which U-Boot
enables at 0xfebf0000::

    => smn md 13d102f0
    13d102f0: febf0001
