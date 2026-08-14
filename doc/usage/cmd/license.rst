.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: license (command)

license command
===============

Synopsis
--------

::

    license

Description
-----------

The license command prints the GNU General Public License, version 2, which is
the licence U-Boot is released under. It takes no arguments and always prints
the whole text, which is 18092 bytes and 339 lines long.

The text is not read from anywhere at runtime. The build turns
'Licenses/gpl-2.0.txt' into a gzip-compressed C array which is linked into
U-Boot, so the command works on a board with no storage and no network. It is
uncompressed into a temporary malloc() buffer each time the command runs, so it
needs about 18KB of free heap; without it the command fails and prints nothing.

The point of the command is to satisfy the requirement that a copy of the
licence accompanies the software, on a device where there is nowhere else to
put one.

Example
-------

The text begins::

    => license
                        GNU GENERAL PUBLIC LICENSE
                           Version 2, June 1991
    ...

and ends::

    This General Public License does not permit incorporating your program into
    proprietary programs.  If your program is a subroutine library, you may
    consider it more useful to permit linking proprietary applications with the
    library.  If this is what you want to do, use the GNU Lesser General
    Public License instead of this License.
    =>

Configuration
-------------

The license command is available if CONFIG_CMD_LICENSE=y. It depends on
CONFIG_GZIP=y for the decompression and selects CONFIG_BUILD_BIN2C=y for the
tool which embeds the text.

Return value
------------

The return value $? is 0 (true) once the text has been printed. It is 1 (false)
if there is not enough memory for the buffer, or if the text does not
uncompress, in which case 'Error uncompressing license text' is printed
instead. Any argument at all is rejected with the usage message.

See also
--------

* :doc:`config<config>` for the .config used to build this image, which is
  embedded in the same way
* *version* for the U-Boot version, compiler and linker used for the build
