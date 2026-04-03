.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cat (command)

cat command
===========

Synopsis
--------

With VFS (CONFIG_CMD_VFS)::

    cat <path>

Legacy (CONFIG_CMD_CAT)::

    cat <interface> <dev[:part]> <file>

Description
-----------

The cat command prints the contents of a file to standard output.

With VFS enabled, the file is read in chunks through the file uclass, so
arbitrary file sizes are supported without loading the entire file into
memory. The filesystem must be mounted first using ``mount``.

path
    absolute path in the VFS, e.g. ``/host/README``

Example
-------

VFS::

    => mount hostfs /host
    => cat /host/README
    # SPDX-License-Identifier: GPL-2.0+
    ...

Legacy::

    => cat mmc 0:1 hello
    hello world

Configuration
-------------

The VFS-based cat command is available when CONFIG_CMD_VFS=y. The legacy
cat command is available when CONFIG_CMD_CAT=y.

Return value
------------

The return value $? is set to 0 (true) if the file was successfully
printed, 1 (false) otherwise.
