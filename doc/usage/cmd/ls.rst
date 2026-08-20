.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ls (command)

ls command
==========

Synopsis
--------

::

    ls [<path>]

Description
-----------

The ls command lists the contents of a directory in the VFS. Entries are
sorted alphabetically. If no path is given, the current working
directory is listed.

Directories are shown with the ``DIR`` prefix. Regular files show their
size in bytes.

The filesystem must be mounted first using ``mount``.

The ``ls`` command is also available as ``fs ls``; see :doc:`fs`.

path
    absolute or relative path to list (default: current working directory)

Example
-------

::

    => mount host 0:0 /mnt
    => ls /mnt
    DIR          0 .
    DIR          0 ..
    DIR          0 subdir
                12 testfile.txt

Configuration
-------------

The ls command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the directory was listed,
1 (false) otherwise.

See also
--------

* :doc:`ext2ls<ext2ls>` for listing an ext filesystem without mounting it
* :doc:`mount<mount>` for making a filesystem available to this command
* :doc:`size<size>` for asking about one file rather than listing them all
