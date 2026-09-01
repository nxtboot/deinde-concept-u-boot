.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cbfsls (command)

cbfsls command
==============

Synopsis
--------

::

    cbfsls

Description
-----------

The cbfsls command lists the files in the CBFS (Coreboot filesystem) which
:doc:`cbfsinit<cbfsinit>` has read, one per line, giving the size in bytes, the
type and the name. A count of the files follows the list.

The list comes from the copy in RAM rather than from the ROM, so it shows the
archive as it stood when cbfsinit ran.

Types are shown by name where the command knows them: bootblock, cbfs header,
stage, payload, fit, option rom, boot splash, raw, vsa, mbi, microcode, fsp,
mrc, mma, efi, struct, cmos default, spd, mrc cache and cmos layout. A type of
0, or of -1 as an erased ROM reads, is shown as *null*. Anything else appears
as the number itself, which is what a coreboot stage does. A file with no name
is listed as *(empty)*.

An archive holding no files at all is reported as an error rather than as an
empty list, since the command cannot tell the two apart. It prints *Success.*,
which is the state the driver is in, and returns a failure.

Example
-------

This lists the small CBFS built by hand in the example on the
:doc:`cbfsinit<cbfsinit>` page, which holds a raw file and a payload::

    => cbfsls
         size              type  name
    ------------------------------------------
           16               raw  hello
           32           payload  u-boot

    2 file(s)

See :doc:`../../board/coreboot/coreboot` for the listing on a real coreboot
machine, which shows the stages as numbers.

Without a CBFS to look at, the command has nothing to list::

    => cbfsls
    CBFS not initialized.

Configuration
-------------

The command is only available if CONFIG_CMD_CBFS=y, which needs
CONFIG_FS_CBFS=y.

Return value
------------

The return value $? is 0 (true) if at least one file is listed. It is 1
(false) if no CBFS has been read, if the CBFS holds no files, or if the command
is given an argument although it takes none, in which case the usage text is
shown.

See also
--------

* :doc:`cbfsinit<cbfsinit>` for reading the CBFS this command lists
* :doc:`cbfsinfo<cbfsinfo>` for showing the header rather than the files
* *cbfsload* for reading one of the listed files into memory
