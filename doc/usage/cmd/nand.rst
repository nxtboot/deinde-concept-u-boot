.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: nand (command)

nand command
============

Synopsis
--------

::

    nand info
    nand device [<dev>]
    nand bad
    nand read[.jffs2|.e|.i|.oob] <addr> <off>|<partition> [<size>]
    nand read.raw <addr> <off>|<partition> [<pages>]
    nand write[.jffs2|.e|.i|.oob|.trimffs] <addr> <off>|<partition> <size>
    nand write.raw[.noverify] <addr> <off>|<partition> [<pages>]
    nand dump[.oob][.ecc] <off>
    nand erase[.spread] [clean] <off> <size>
    nand erase.part [clean] <partition>
    nand erase.chip [clean]
    nand scrub [-y] <off> <size>
    nand scrub.part [-y] <partition>
    nand scrub.chip [-y]
    nand markbad <off> [<off> ...]
    nand biterr <off> <bit>
    nand lock [tight|status]
    nand unlock[.allexcept] [<off> <size>]
    nand watch <off> <size>
    nand watch.part[.quiet] <partition>
    nand watch.chip[.quiet]
    nand torture <off> [<size>]
    nand env.oob get
    nand env.oob set <off>|<partition>

Description
-----------

The nand command operates on raw NAND flash: the chips registered by a raw
NAND controller driver, as opposed to the SPI-NAND and NOR devices which
only the generic MTD command reaches. It works on one chip at a time, the
'current device', which *nand device* selects and which every sub-command
uses unless a partition name names another.

Sub-commands
~~~~~~~~~~~~

info
    show the geometry of every chip, and set the environment variables
    described below from the last of them

device
    with no argument, show the current device; with one, make that device
    current

bad
    list the offset of every block of the current device which is marked
    bad, saying which of them the bad-block table has reserved for itself

read
    copy <size> bytes from the device into memory at <addr>

write
    copy <size> bytes from memory at <addr> onto the device

dump
    read a single page and print it as a hex dump, rather than storing it
    in memory

erase
    erase <size> bytes of the device. With '.part' erase a whole partition
    instead, and with '.chip' the whole device. The word 'clean' after the
    sub-command writes a JFFS2 clean marker into the spare area of each
    block as it goes

scrub
    erase as above, but erase the blocks marked bad as well. The markers
    set at the factory cannot be recovered once this has run, so the
    command asks for confirmation unless -y is given

markbad
    mark the block holding each given offset as bad

biterr
    flip bit <bit> of the byte at <off>, to make a read error on purpose.
    Bits are counted from 0, and may run past 7 to reach a later byte

lock, unlock
    bring the chip into its locked state, report which pages are locked, or
    unlock a range of it. 'lock tight' makes the lock itself unchangeable
    until the next power cycle. Available only when
    CONFIG_CMD_NAND_LOCK_UNLOCK is enabled

watch
    read an area and report how many bitflips the ECC had to correct in
    each page, which says how close the blocks are to wearing out. '.quiet'
    gives the summary alone. Available only when CONFIG_CMD_NAND_WATCH is
    enabled

torture
    erase a block, write several patterns to it and read them back, to
    decide whether a block which has failed a write is still good. This
    destroys the data in the range. Available only when
    CONFIG_CMD_NAND_TORTURE is enabled

env.oob
    report, or set, the block holding the environment, which is recorded in
    the spare area of block 0 of the first device. Available only when
    CONFIG_ENV_OFFSET_OOB is enabled

Suffixes
~~~~~~~~

The read, write and dump sub-commands take a suffix to vary what they do:

.raw
    bypass the ECC engine, so each page is transferred exactly as it sits
    in the chip, its spare area following its data. The length is then a
    number of pages rather than a number of bytes

.noverify
    for write.raw alone, skip the read-back which otherwise checks every
    page written

.oob
    transfer the spare area rather than the data

.ecc
    for dump alone, print the page as the ECC engine corrects it rather
    than as it sits in the chip

.trimffs
    for write alone, drop any page at the end of an erase block whose every
    byte is 0xff, which leaves it erased and saves a program cycle.
    Available only when CONFIG_CMD_NAND_TRIMFFS is enabled

.jffs2, .e, .i
    accepted and ignored, for compatibility with old scripts

.spread
    for erase alone, make <size> the amount of good flash to erase, so bad
    blocks passed over do not count against it

Arguments
~~~~~~~~~

<addr> is a memory address and <off> and <size> are offsets into the
device, all in hex. <dev> and <bit> are in decimal.

Wherever an offset is wanted a partition name may be given instead, as
*mtdparts* defines it; the offset and the size then come from the
partition. Note that the offset the command reports is the absolute one,
counted from the start of the chip.

The size of a read or a write may be left out, in which case it covers
what is left of the device or of the partition, less whatever the bad
blocks in that range take up. erase wants both numbers and produces a
usage message without them, rather than erasing the lot by accident.

An offset at or past the end of the device, or a size which reaches past
it, is refused before anything happens, with 'Offset exceeds device limit'
or 'Size exceeds partition or device limit'.

Bad blocks
~~~~~~~~~~

read and write skip any block marked bad without counting it against
<size>, so a transfer moves <size> bytes of good flash and ends further
into the device than <size> alone would suggest. erase keeps to the range
it is given instead, passing over the bad blocks in it.

A read whose ECC corrects one or more bitflips succeeds and says nothing
about it: the data is good, and all the device is reporting is that the
number of corrected flips in a page has reached the threshold at which it
is worth noting. Only a read the ECC cannot put right fails.

Environment
~~~~~~~~~~~

*nand info*, and *nand device* with no argument, set three variables
describing the chip they have just printed: ``nand_writesize``,
``nand_oobsize`` and ``nand_erasesize``, each in hex. Since *nand info*
prints every chip in turn, it leaves the variables describing the last of
them rather than the current device.

*nboot* takes the device to boot from the ``bootdevice`` variable when it
is not given one.

Example
-------

The sandbox build emulates two NAND chips, which makes a convenient
illustration::

    => nand info

    Device 0: nand0, sector size 8 KiB
      Page size          512 b
      OOB size            16 b
      Erase size        8192 b
      ecc strength         1 bits
      ecc step size      256 b
      subpagesize        256 b
      options       0x00000100
      bbt options   0x00000000
    Device 1: nand1, sector size 512 KiB
      Page size         4096 b
      OOB size           224 b
      Erase size      524288 b
      ecc strength         4 bits
      ecc step size      512 b
      subpagesize       1024 b
      options       0x00005000
      bbt options   0x00000000

The second chip can be made current, and shows the same report on its
own::

    => nand device 1
    Device 1: nand1... is now current device
    => nand device

    Device 1: nand1, sector size 512 KiB
      Page size         4096 b
    ...

A block must be erased before it can be written, and a page written from
memory can be read back into memory again::

    => nand device 0
    Device 0: nand0... is now current device
    => nand erase 0 2000

    NAND erase: device 0 offset 0x0, size 0x2000
    Erasing at 0x0 -- 100% complete.
    OK
    => mw.b 1000 5a 200
    => nand write 1000 0 200

    NAND write: device 0 offset 0x0, size 0x200
     512 bytes written: OK
    => nand read 2000 0 200

    NAND read: device 0 offset 0x0, size 0x200
     512 bytes read: OK
    => cmp.b 1000 2000 200
    Total of 512 byte(s) were the same

dump prints the same page without needing an address. The emulated chips
flip as many bits per ECC step as their threshold allows, so '.ecc' is
needed to see what was written rather than what the ECC has yet to
correct::

    => nand dump.ecc 0

    Page at offset 00000000 dump:
    00000000: 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a  ZZZZZZZZZZZZZZZZ
    ...
    000001f0: 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a  ZZZZZZZZZZZZZZZZ

    OOB:
    00000000: ff ff ff ff ff ff ff ff  ........
    00000008: ff ff ff ff ff ff ff ff  ........

A deliberate bit error is one the single-bit ECC of this chip cannot put
right, so the dump fails and shows the uncorrected page instead::

    => nand biterr 0 3
    Flip data at 0x0 with xor 0x08 (bit=3) to value=0x52
    => nand dump.ecc 0
    Error reading page at offset 00000000, -74 uncorrectable, dumping raw data

    Page at offset 00000000 dump:
    ...

With a partition table in place, a partition can be named instead of an
offset, and the command then reports where in the chip it starts::

    => setenv mtdids nand0=nand0
    => setenv mtdparts nand0:1m(boot),2m(kernel),-(rootfs)
    => mw.b 1000 a5 200
    => nand write 1000 kernel 200

    NAND write: device 0 offset 0x100000, size 0x200
     512 bytes written: OK
    => nand erase.part boot

    NAND erase.part: device 0 offset 0x0, size 0x100000
    Erasing at 0x0 -- 100% complete.
    OK

Neither emulated chip has a bad block to start with, but one can be
marked::

    => nand bad

    Device 0 bad blocks:
    => nand markbad 2000
    block 0x00002000 successfully marked as bad
    => nand bad

    Device 0 bad blocks:
      0x00002000

Asking for a device which is not there reports it::

    => nand device 5
    no devices available

Return value
------------

The return value $? is 0 (true) if the operation succeeds and 1 (false) if
it fails, for instance because there is no such device, because an offset
or a partition name cannot be resolved, or because a read could not be
corrected by the ECC.

A missing or unrecognised sub-command, or one given the wrong number of
arguments, produces a usage message and sets $? to 1.

Note that 'nand bad' returns 0 whatever it finds, and that 'nand torture'
returns the number of blocks which failed rather than 0 or 1.

See also
--------

* :doc:`mtd<mtd>` for the generic command, which reaches these chips as
  well as every other memory technology device
* :doc:`mtdparts<mtdparts>` for defining the partitions this command can
  name
* :doc:`chpart<chpart>` for choosing which of those partitions is current
* :doc:`sf<sf>` for SPI flash, which this command does not reach
* *nboot* for booting an image held in raw NAND
