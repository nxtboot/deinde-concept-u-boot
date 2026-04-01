.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: cd (command)

cd command
==========

Synopsis
--------

::

    cd [<path>]

Description
-----------

The ``cd`` command changes the VFS current working directory. When a
relative path (one that does not start with ``/``) is given to commands
like ``ls``, ``cat``, ``load``, ``save`` or ``size``, it is resolved
relative to the current working directory.

With no argument, ``cd`` returns to the root directory ``/``.

path
    Absolute or relative path (default ``/``)

Example
-------

::

    => mount hostfs /host
    => cd /host
    => pwd
    /host
    => ls
    DIR       4096 arch
    DIR      12288 cmd
       ...
    => cd /
    => pwd
    /

Configuration
-------------

The cd command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is set to 0 (true) if the directory change
succeeded, 1 (false) otherwise.
