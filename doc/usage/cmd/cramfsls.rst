.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cramfsls (command)

cramfsls command
================

Synopsis
--------

::

    cramfsls [directory]

Description
-----------

The cramfsls command lists the contents of a directory in a CRAMFS image.
CRAMFS is a small read-only compressed filesystem, normally kept in flash
rather than on a block device, so the command does not name an interface or
a partition. It reads the image from memory, at the address held in the
'cramfsaddr' environment variable, which must be set first.

Each entry is shown with its mode, its size in bytes and its name. A
directory is shown with a mode beginning d and the size of its directory
entries, not of what it holds.

Naming a file rather than a directory lists that one file, so this is also
the way to ask whether a file is present.

directory
    path of the directory to list, defaults to /

Example
-------

This uses a CRAMFS image loaded to 0x1000000 on sandbox, holding a file
hello.txt of 14 bytes and a directory subdir::

    => setenv cramfsaddr 1000000
    => cramfsls
     -rw-rw-r--       14 hello.txt
     drwxrwxr-x       20 subdir
    => cramfsls subdir
     -rw-rw-r--       10 deep.txt
    => cramfsls hello.txt
     -rw-rw-r--       14 hello.txt

A path which is not there is reported::

    => cramfsls missing
    can't find corresponding entry
    => echo $?
    1

An address which does not hold a CRAMFS image prints nothing, so the return
value is the only sign of what happened::

    => setenv cramfsaddr 2000000
    => cramfsls
    => echo $?
    1

The variable has to be set at all::

    => setenv cramfsaddr
    => cramfsls
    Environment variable 'cramfsaddr' is not set
    => echo $?
    1

Configuration
-------------

The cramfsls command is only available if CONFIG_CMD_CRAMFS=y, which needs
CONFIG_FS_CRAMFS=y.

Return value
------------

The return value $? is set to 0 (true) if at least one entry was listed,
and to 1 (false) otherwise, which covers a missing 'cramfsaddr', an address
holding no CRAMFS image, and a path which the image does not contain.

See also
--------

* *cramfsload* for reading one of the listed files into memory
* :doc:`ls<ls>` for listing a directory on a filesystem held on a block
  device rather than in memory
* :doc:`fsuuid<fsuuid>` for the UUID of such a filesystem, which CRAMFS
  does not have
