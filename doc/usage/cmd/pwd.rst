.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: pwd (command)

pwd command
===========

Synopsis
--------

::

    pwd

Description
-----------

The ``pwd`` command prints the current VFS working directory. The
working directory is changed with :doc:`cd`.

Example
-------

::

    => cd /host
    => pwd
    /host
    => cd /
    => pwd
    /

Configuration
-------------

The pwd command is available when CONFIG_CMD_VFS=y.

Return value
------------

The return value $? is always set to 0 (true).
