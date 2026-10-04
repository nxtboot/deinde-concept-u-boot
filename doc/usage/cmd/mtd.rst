.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: mtd (command)

mtd command
===========

Synopsis
--------

::

    mtd list
    mtd read[.raw][.oob][.benchmark] <name> <addr> [<off> [<size>]]
    mtd dump[.raw][.oob] <name> [<off> [<size>]]
    mtd write[.raw][.oob][.dontskipff][.benchmark] <name> <addr> [<off> [<size>]]
    mtd erase[.dontskipbad] <name> [<off> [<size>]]
    mtd bad <name>
    mtd markbad <name> <off> [<off> ...]
    mtd otpread <name> [u|f] <off> <size>
    mtd otpwrite <name> <off> <hex-string>
    mtd otplock <name> <off> <size>
    mtd otpinfo <name> [u|f]
    mtd nand_write_test <name> [<off> [<size>]]
    mtd nand_read_test <name>

Description
-----------

The mtd command operates on memory technology devices: raw NAND, NOR and
SPI flash, dataflash and anything else which registers with the MTD stack.
It replaces the older per-technology commands, so a single set of
sub-commands reaches every such device, whatever bus it sits on.

A device is named either by its MTD name (as *mtd list* reports it), by the
name of the driver-model device behind it, or by its devicetree path. A
partition is named in the same way, and an offset within a partition is
relative to the start of that partition rather than to the start of the
chip.

Sub-commands
~~~~~~~~~~~~

list
    show every device, its geometry and its partitions

read
    copy <size> bytes from the device into memory at <addr>

dump
    read <size> bytes and print them as a hex dump, rather than storing
    them in memory

write
    copy <size> bytes from memory at <addr> onto the device

erase
    erase <size> bytes of the device

bad
    list the offset of every block marked bad

markbad
    mark each block containing one of the given offsets as bad. Available
    only when CONFIG_CMD_MTD_MARKBAD is enabled

otpread, otpwrite, otplock, otpinfo
    read, write, lock and report the one-time-programmable region, which is
    either the user region ('u') or the factory region ('f'). The hex string
    given to otpwrite carries no '0x' and no spaces, as in ABCD1234, and the
    command asks for confirmation before writing. Available only when
    CONFIG_CMD_MTD_OTP is enabled

nand_write_test
    erase each block in turn, write three patterns to it and read them back,
    to decide whether a block which has failed a write is still good. This
    destroys the data in the range. Available only when
    CONFIG_CMD_MTD_NAND_WRITE_TEST is enabled

nand_read_test
    read every block both raw and through the ECC and compare the two, to
    find blocks whose bitflip count says they are wearing out. Available
    only when CONFIG_CMD_MTD_NAND_READ_TEST is enabled

Suffixes
~~~~~~~~

The read, dump, write and erase sub-commands take a suffix, or several, to
vary what they do:

.raw
    bypass the ECC engine, so the data and the spare area are transferred
    exactly as they sit in the chip

.oob
    transfer the out-of-band area of each page as well as its data. For
    read and write the OOB of each page follows all of the data in memory;
    for dump it is printed after each page

.dontskipff
    write a page even when every byte of it, data and OOB, is 0xff. Without
    this, such a page is skipped, which leaves it erased and saves the wear
    of a program cycle

.dontskipbad
    erase a block even when it is marked bad, which is the only way to clear
    a marker set in error

.benchmark
    report the transfer rate once the operation has finished

Arguments
~~~~~~~~~

<addr> is a memory address, in hex. <off> and <size> are also in hex, and
between them default to covering the whole device: <off> to 0 and <size> to
whatever is left from <off> to the end of the device. The exception is dump,
where <size> defaults to a single page.

Both numbers are checked before anything happens, and an operation which
would run past the end of the device is refused. erase wants <off> and
<size> on a block boundary and refuses the operation otherwise. The others
want <off> on a page boundary, again refusing otherwise, but a <size> which
is not a multiple of the page size is rounded up rather than refused.

Bad blocks
~~~~~~~~~~

read, write and dump skip any block marked bad without counting it against
<size>, so a transfer moves <size> bytes of good flash and ends further
into the device than <size> alone would suggest. It fails if it runs out of
good blocks before it has moved that much. erase keeps to the range it is
given instead, reporting each bad block it passes over.

erase.dontskipbad and markbad are the two ways to act on a bad block
deliberately.

A read whose ECC corrects one or more bitflips succeeds and says nothing
about it: the data is good, and all the device is reporting is that the
number of corrected flips in a page has reached the threshold at which it is
worth noting. Only a read the ECC cannot put right fails.

Example
-------

The sandbox build emulates two NAND chips, which makes a convenient
illustration::

    => mtd list
    List of MTD devices:
    * nand0
      - device: nand-controller
      - parent: root_driver
      - driver: sand-nand
      - path: /nand-controller
      - type: NAND flash
      - block size: 0x2000 bytes
      - min I/O: 0x200 bytes
      - OOB size: 16 bytes
      - OOB available: 8 bytes
      - ECC strength: 1 bits
      - ECC step size: 256 bytes
      - bitflip threshold: 1 bits
      - 0x000000000000-0x000000400000 : "nand0"
    * nand1
      - device: nand-controller
      - parent: root_driver
      - driver: sand-nand
      - path: /nand-controller
      - type: NAND flash
      - block size: 0x80000 bytes
      - min I/O: 0x1000 bytes
      - OOB size: 224 bytes
      - OOB available: 166 bytes
      - ECC strength: 4 bits
      - ECC step size: 512 bytes
      - bitflip threshold: 3 bits
      - 0x000000000000-0x000100000000 : "nand1"

A block must be erased before it can be written, and a page written from
memory can be read back into memory again::

    => mtd erase nand0 0 2000
    Erasing 0x00000000 ... 0x00001fff (1 eraseblock(s))
    => mw.b 1000 5a 200
    => mtd write nand0 1000 0 200
    Writing 512 byte(s) (1 page(s)) at offset 0x00000000
    => mtd read nand0 2000 0 200
    Reading 512 byte(s) (1 page(s)) at offset 0x00000000
    => cmp.b 1000 2000 200
    Total of 512 byte(s) were the same

The emulated chips flip as many bits per ECC step as their threshold allows,
so every read of them is one the ECC has had to correct. dump prints the
same page without needing an address, sixteen bytes to a line::

    => mtd dump nand0 0 200
    Reading 512 byte(s) (1 page(s)) at offset 0x00000000

    Dump 512 data bytes from 0x00000000:
    0x00000000:	5a 5a 5a 5a 5a 5a 5a 5a  5a 5a 5a 5a 5a 5a 5a 5a
    0x00000010:	5a 5a 5a 5a 5a 5a 5a 5a  5a 5a 5a 5a 5a 5a 5a 5a
    ...
    0x000001f0:	5a 5a 5a 5a 5a 5a 5a 5a  5a 5a 5a 5a 5a 5a 5a 5a

With a partition table in place, a partition can be named instead of the
chip, and the offset then counts from the start of the partition::

    => setenv mtdids nand0=nand0
    => setenv mtdparts nand0:1m(boot),2m(kernel),-(rootfs)
    => mtd erase kernel 0 2000
    Erasing 0x00000000 ... 0x00001fff (1 eraseblock(s))
    => mw.b 1000 a5 200
    => mtd write kernel 1000 0 200
    Writing 512 byte(s) (1 page(s)) at offset 0x00000000

The same page read through the chip appears at 1MB, where the partition
starts::

    => mtd dump nand0 100000 200
    Reading 512 byte(s) (1 page(s)) at offset 0x00100000

    Dump 512 data bytes from 0x00100000:
    0x00100000:	a5 a5 a5 a5 a5 a5 a5 a5  a5 a5 a5 a5 a5 a5 a5 a5
    ...

Neither emulated chip has a bad block, so the list of them is empty::

    => mtd bad nand0
    MTD device nand0 bad blocks list:

An offset which is not page-aligned is refused, while a size which is not a
multiple of the page size is rounded up::

    => mtd dump nand0 100
    Offset not aligned with a page (0x200)
    => mtd dump nand0 0 40
    Size not on a page boundary (0x200), rounding to 0x200
    Reading 512 byte(s) (1 page(s)) at offset 0x00000000

An operation must fit within the device. The last block of nand0 starts at
0x3fe000, so a read from there picks up the last 0x2000 bytes by default
and a larger size is refused::

    => mtd read nand0 1000000 3fe000
    Reading 8192 byte(s) (16 page(s)) at offset 0x003fe000
    => mtd read nand0 1000000 3fe000 4000
    Op does not fit in nand0 (400000)
    => mtd erase nand0 400000
    Op does not fit in nand0 (400000)

Naming something which is not a device reports the error from the MTD
stack::

    => mtd read foo 1000
    MTD device foo not found, ret -19

Return value
------------

The return value $? is 0 (true) if the operation succeeds and 1 (false) if
it fails, for instance because the device named does not exist, because an
offset or a size is misaligned or does not fit the device, or because a
read could not be corrected by the ECC.

A missing or unrecognised sub-command, or a read or write with no <addr>,
produces a usage message and sets $? to 1.

Note that 'mtd bad' returns 0 even for a device which cannot have bad
blocks at all, having said so, and that 'mtd erase' takes -EIO from a block
as success, so a block which refuses to erase does not fail the command.

See also
--------

* :doc:`mtdparts<mtdparts>` for splitting a device into the named partitions
  this command can address
* :doc:`chpart<chpart>` for choosing which of those partitions is current
* :doc:`sf<sf>` for SPI flash operations which name a device by bus and chip
  select rather than by MTD name
* :doc:`nand<nand>` for the older NAND-only command, which this one
  supersedes
