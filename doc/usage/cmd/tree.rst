.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: tree (command)

tree command
============

Synopsis
--------

::

    tree [<path>]

Description
-----------

The tree command recursively lists the contents of a directory and its
subdirectories in a tree-like format, up to three levels deep. If no
path is given, the current working directory is listed.

The filesystem must be mounted first using ``mount``.

path
    absolute or relative path to list (default: current working directory)

Example
-------

::

    => mount host 0:0 /mnt
    => tree /mnt
    /mnt
    |-- subdir
    |   `-- nested.txt
    |-- lost+found
    `-- testfile.txt

Configuration
-------------

The tree command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the directory was listed,
1 (false) otherwise.
