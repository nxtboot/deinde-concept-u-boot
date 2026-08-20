.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: save (command)

save command
============

Synopsis
--------

::

    save <addr> <path> <bytes> [pos]

Description
-----------

The save command writes data from memory to a file via the VFS. The
filesystem must be mounted first using ``mount``.

addr
    memory address to read from (hexadecimal)

path
    absolute path in the VFS, e.g. ``/host/output.bin``

bytes
    number of bytes to write (hexadecimal)

pos
    file byte position to start writing at (hexadecimal, default 0)

Example
-------

::

    => mount hostfs /host
    => save 1000000 /host/data.bin 1000
    4096 bytes written
    => save 1000000 /host/data.bin 100 800
    256 bytes written

Configuration
-------------

The save command is available when CONFIG_CMD_VFS=y, which depends on
CONFIG_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the file was successfully
written, 1 (false) otherwise.

See also
--------

* :doc:`fatwrite<fatwrite>` for the FAT-only form of this command
* :doc:`load<load>` for reading the data back into memory
* :doc:`mount<mount>` for making a filesystem available to the VFS
