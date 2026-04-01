.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: stat (command)

stat command
============

Synopsis
--------

::

    stat <path>

Description
-----------

The stat command displays metadata about a file or directory in the VFS,
including its name, size, type and timestamps (modification, access and
creation times where available).

The filesystem must be mounted first using ``mount``.

path
    absolute or relative path in the VFS

Example
-------

::

    => mount hostfs /host
    => stat /host/README
      File: README
      Size: 84732
      Type: regular file
    Modify: 2025-03-15 10:30:00
    Access: 2025-03-23 08:15:22
    => stat /host/cmd
      File: cmd
      Size: 4096
      Type: directory

Configuration
-------------

The stat command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the file was found, 1 (false)
otherwise.
