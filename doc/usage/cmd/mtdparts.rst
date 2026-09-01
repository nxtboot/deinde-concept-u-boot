.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: mtdparts (command)

mtdparts command
================

Synopsis
--------

::

    mtdparts
    mtdparts add <mtd-dev> <size>[@<offset>] [<name>] [ro]
    mtdparts add.spread <mtd-dev> <size>[@<offset>] [<name>] [ro]
    mtdparts del <part-id>
    mtdparts delall
    mtdparts default
    mtdparts spread

Description
-----------

The mtdparts command maintains a partition table for the MTD devices in the
system, in the same command-line format the Linux kernel uses. The table is
held in the environment, so it survives a reboot once the environment is
saved, and it can be handed to the kernel unchanged.

Three environment variables are involved:

mtdids
    maps a U-Boot device to the MTD name the kernel uses for it, as
    ``<dev-id>=<mtd-id>``. <dev-id> is 'nand', 'nor', 'onenand' or 'spi-nand'
    followed by the device number, and <mtd-id> is the name of the MTD device
    (as shown by *mtd list*). Several mappings are separated by commas.

mtdparts
    the partition table itself, as ``<mtd-id>:<part-def>[,<part-def>...]``.
    Each <part-def> is ``<size>[@<offset>][(<name>)][ro]``, where <size> is a
    Linux memsize such as 1m or 512k, or '-' for all the remaining space.
    Several devices are separated by semicolons. A leading 'mtdparts=' is
    accepted and ignored, so a kernel command-line fragment can be used
    unchanged; the value this command writes back has no such prefix.

partition
    the partition selected as current, as ``<dev-id>,<part-num>``. It is
    written by this command and by :doc:`chpart<chpart>`, and read by the
    commands which act on the current partition.

With no arguments the command lists the partitions of every device, marks the
current one and reports the built-in defaults.

mtdparts must be able to read mtdids before it can do anything, so a board
with neither an mtdids variable nor a CONFIG_MTDIDS_DEFAULT value cannot use
this command.

add
    appends a partition to <mtd-dev>. Without an offset the partition starts
    where the previous one ends. The 'ro' keyword marks it read-only, which
    shows as a mask_flags value of 1 and is passed on to the kernel.

add.spread
    the same, but pads the partition so that it is at least <size> bytes of
    good blocks, skipping bad ones. Available when CONFIG_CMD_MTDPARTS_SPREAD
    is enabled.

del
    removes one partition, named as <dev-id>,<part-num>.

delall
    removes every partition and unsets the mtdparts variable.

default
    unsets mtdids, mtdparts and partition, so that the built-in defaults apply
    again.

spread
    grows every partition so that each holds its stated size in good blocks
    and starts on a good block. Available when CONFIG_CMD_MTDPARTS_SPREAD is
    enabled.

This command is deprecated. New boards should use the MTD stack and the *mtd*
command, which reads its partitions from the devicetree.

Example
-------

This partitions the sandbox NAND device into three::

    => setenv mtdids nand0=nand0
    => setenv mtdparts nand0:1m(boot),2m(kernel),-(rootfs)
    => mtdparts

    device nand0 <nand0>, # parts = 3
     #: name                size            offset          mask_flags
     0: boot                0x00100000      0x00000000      0
     1: kernel              0x00200000      0x00100000      0
     2: rootfs              0x00100000      0x00300000      0

    active partition: nand0,0 - (boot) 0x00100000 @ 0x00000000

    defaults:
    mtdids  : none
    mtdparts: none

Partitions can be edited one at a time, and the mtdparts variable is
regenerated from the table after each change::

    => mtdparts del nand0,2
    => mtdparts add nand0 512k spare
    => printenv mtdparts
    mtdparts=nand0:1m(boot),2m(kernel),512k(spare)

Marking a partition read-only sets its mask_flags::

    => setenv mtdparts nand0:1m(boot)ro,2m(kernel)
    => mtdparts

    device nand0 <nand0>, # parts = 2
     #: name                size            offset          mask_flags
     0: boot                0x00100000      0x00000000      1
     1: kernel              0x00200000      0x00100000      0

    active partition: nand0,0 - (boot) 0x00100000 @ 0x00000000

    defaults:
    mtdids  : none
    mtdparts: none

Without mtdids there is nothing to partition::

    => mtdparts
    mtdids not defined, no default present

Return value
------------

The return value $? is 0 (true) if the command succeeds and 1 (false) if it
fails, for instance because mtdids is not set, because the partition table
does not fit the device, or because the partition named for 'del' does not
exist.

An unrecognised sub-command produces a usage message and $? is set to 1.

Note that 'mtdparts default' returns 0 even when the board has no defaults to
fall back on, since it has done what was asked of it: the variables are unset.

See also
--------

* :doc:`chpart<chpart>` for choosing which of these partitions is current
* :doc:`sf<sf>` for reading and writing SPI flash, which can be partitioned
  this way
* *mtd* for the MTD stack command which supersedes this one
* *nand* for NAND operations, which accept a partition name defined here
