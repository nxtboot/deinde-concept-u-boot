.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: mca (command)

mca command
===========

Synopsis
--------

::

    mca [all | clear]

Description
-----------

The mca command shows the processor's machine-check banks, as used by AMD
EPYC Turin's Scalable MCA. It first shows the number of banks, then each bank
which holds a valid error, with its IPID (which block the bank belongs to),
status, address, miscellaneous, syndrome and configuration registers. This is
useful when a machine check has reset the board, since the banks keep their
contents across a warm reset.

all
    Show every bank, whether or not it holds an error.

clear
    Show the banks with an error, then clear their status.

Configuration
-------------

The mca command is available on AMD EPYC Turin.

Example
-------

::

    => mca
    32 banks
    => mca all
    32 banks
    bank  0: ipid 000000b0200ea200 status 0000000000000000 addr 0000000000000000
             misc d010000000000000 synd 0000000000000000 config 00000002000001ef
    bank  1: ipid 000100b0200eaa00 status 0000000000000000 addr 0000000000000000
             misc d010000000000000 synd 0000000000000000 config 00000002000001eb
    ...
