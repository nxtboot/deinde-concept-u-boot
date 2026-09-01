.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: chpart (command)

chpart command
==============

Synopsis
--------

::

    chpart <part-id>

Description
-----------

The chpart command selects one of the MTD partitions defined by
:doc:`mtdparts<mtdparts>` as the current one. Commands which act on 'the
current partition' use whichever one was chosen last.

part-id
    the partition to select, as ``<dev-id>,<part-num>``. <dev-id> is 'nand',
    'nor', 'onenand' or 'spi-nand' followed by the device number, and
    <part-num> counts from 0 in the order the partitions are listed.

The choice is written to the 'partition' environment variable, so it survives
a reboot once the environment is saved. Setting that variable by hand has the
same effect, since :doc:`mtdparts<mtdparts>` reads it back.

The partition table has to exist before a partition can be chosen from it, so
chpart reports the same mtdids and mtdparts problems that
:doc:`mtdparts<mtdparts>` does.

Example
-------

This selects the third partition of the sandbox NAND device::

    => setenv mtdids nand0=nand0
    => setenv mtdparts nand0:1m(boot),2m(kernel),-(rootfs)
    => chpart nand0,2
    partition changed to nand0,2
    => printenv partition
    partition=nand0,2

The partition number is checked against the table::

    => chpart nand0,9
    invalid partition number 9 for device nand0 (nand0)
    no such partition

as is the device::

    => chpart nand5,0
    no such device nand5

Return value
------------

The return value $? is 0 (true) if the partition was selected and 1 (false)
if it was not, whether because no part-id was given, because the partition
table cannot be read, or because the device or partition named does not
exist.

See also
--------

* :doc:`mtdparts<mtdparts>` for defining the partitions this command chooses
  between
* *mtd* for the MTD stack command which supersedes both
* *nand* for NAND operations, which act on the partition chosen here
