.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cbfsinit (command)

cbfsinit command
================

Synopsis
--------

::

    cbfsinit [<end of rom>]

Description
-----------

The cbfsinit command finds a CBFS (Coreboot filesystem) in memory and reads its
list of files into RAM, so that the other cbfs commands have something to work
with. A CBFS is the archive coreboot keeps its own pieces in (the bootblock,
the payload, microcode and so on) and it normally sits in SPI flash which the
chipset maps into the top of the address space. See
:doc:`../../board/coreboot/coreboot` for what one looks like on a real board.

end of rom
    address of the last byte of the ROM holding the CBFS. It defaults to
    0xffffffff, which is where the top of the flash appears on a 32-bit x86
    machine

The last four bytes of the ROM hold a little-endian offset which the command
adds to the address just past the end of the ROM to reach the master header.
With the default end of ROM that sum wraps round on a 32-bit machine, so those
four bytes are simply the address of the header.

The header gives the size of the ROM and the alignment of the files in it. The
command then walks the whole ROM at that alignment looking for file headers,
and caches the name, type, size and position of each file it finds. Nothing
reads the ROM again until cbfsinit runs anew, so a command which changes the
ROM has to be followed by another cbfsinit.

A failed cbfsinit leaves the driver uninitialised, whatever state it was in
before, so the other cbfs commands go back to reporting 'CBFS not initialized'.

Example
-------

Sandbox has no ROM, so this builds a very small CBFS in memory by hand. The
master header lies at the start of the ROM, the two files at 0x40 and 0x80, and
the offset back to the header in the last four bytes. All the fields of the
CBFS are big-endian, so the values given to *mw* read backwards::

    => mw.b 1000000 0 100
    => mw.l 1000000 4342524f
    => mw.l 1000004 32313131
    => mw.l 1000008 00010000
    => mw.l 1000010 40000000 2
    => mw.l 1000040 4352414c
    => mw.l 1000044 45564948
    => mw.l 1000048 10000000
    => mw.l 100004c 50000000
    => mw.l 1000054 20000000
    => mw.l 1000058 6c6c6568
    => mw.l 100005c 0000006f
    => mw.l 1000060 12345678 4
    => mw.l 1000080 4352414c
    => mw.l 1000084 45564948
    => mw.l 1000088 20000000 2
    => mw.l 1000094 20000000
    => mw.l 1000098 6f622d75
    => mw.l 100009c 0000746f
    => mw.l 10000a0 aabbccdd 8
    => mw.l 10000fc ffffff00

That gives a 256-byte ROM ending at 0x10000ff, holding a 16-byte raw file
called hello and a 32-byte payload called u-boot. The command says nothing
when it works::

    => cbfsinit 10000ff
    => cbfsls
         size              type  name
    ------------------------------------------
           16               raw  hello
           32           payload  u-boot

    2 file(s)

Clearing the header magic is enough to make it fail, and the listing then has
nothing to show::

    => mw.l 1000000 0
    => cbfsinit 10000ff
    Bad CBFS header.
    => cbfsls
    CBFS not initialized.

Configuration
-------------

The command is only available if CONFIG_CMD_CBFS=y, which needs
CONFIG_FS_CBFS=y.

Return value
------------

The return value $? is 0 (true) if the CBFS is read. It is 1 (false) if the
'end of rom' argument is not a hexadecimal number, or if the master header has
the wrong magic value or puts the start of the files past the end of the ROM.

Note that the command reads the address it is given without checking it, so a
wrong value is as likely to crash U-Boot as to be reported.

See also
--------

* :doc:`cbfsinfo<cbfsinfo>` for showing the header this command reads
* :doc:`cbfsls<cbfsls>` for listing the files this command finds
* *cbfsload* for reading one of those files into memory
* :doc:`cbsysinfo<cbsysinfo>` for the coreboot sysinfo table, which gives the
  offset and size of the CBFS on a board which really has one
* :doc:`sf<sf>` for reading the SPI flash a CBFS normally lives in
