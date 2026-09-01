.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cbfsinfo (command)

cbfsinfo command
================

Synopsis
--------

::

    cbfsinfo

Description
-----------

The cbfsinfo command shows the master header of the CBFS (Coreboot
filesystem) which :doc:`cbfsinit<cbfsinit>` has read, so it says nothing about
the ROM until that command has run.

The fields are those of the header itself, in the order coreboot writes them:

CBFS version
    version of the header format, normally 0x31313132

ROM size
    size of the whole ROM, which is where the search for the header started
    from

Boot block size
    size of the bootblock at the end of the ROM, which holds the first code the
    machine runs and is not part of the archive

CBFS size
    size of the archive itself, being the ROM size less the bootblock and less
    the offset below

Alignment
    boundary the files sit on, in bytes. This is the step the scan for files
    takes, so it is also the smallest a file can be

Offset
    where the files start within the ROM

Example
-------

This shows the header of the small CBFS built by hand in the example on the
:doc:`cbfsinit<cbfsinit>` page, a 256-byte ROM with no bootblock and its files
starting at 0x40::

    => cbfsinfo

    CBFS version: 0x31313132
    ROM size: 0x100
    Boot block size: 0x0
    CBFS size: 0xc0
    Alignment: 64
    Offset: 0x40

See :doc:`../../board/coreboot/coreboot` for the same command on a real
coreboot machine.

Without a CBFS to look at, the command has nothing to show::

    => cbfsinfo
    CBFS not initialized.

Configuration
-------------

The command is only available if CONFIG_CMD_CBFS=y, which needs
CONFIG_FS_CBFS=y.

Return value
------------

The return value $? is 0 (true) if the header is shown and 1 (false) if no
CBFS has been read. It is also 1 when the command is given an argument, since
it takes none, in which case the usage text is shown.

See also
--------

* :doc:`cbfsinit<cbfsinit>` for reading the CBFS this command reports on
* :doc:`cbfsls<cbfsls>` for listing the files rather than the header
* *cbfsload* for reading one of those files into memory
