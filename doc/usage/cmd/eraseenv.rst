.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: eraseenv (command)

eraseenv command
================

Synopsis
--------

::

    eraseenv

Description
-----------

The eraseenv command erases the environment held in persistent storage. The
environment in memory is left alone, so the variables carry on working until
the next boot, or until they are read back with 'env load'. Since the stored
copy is protected by a checksum, reading it back after an erase reports a bad
CRC and falls back to the default environment.

The storage which is erased is the one chosen by 'env select', or the highest
priority location if that command has not been used. Not every location can
be erased: those which provide no erase operation report 'not possible'
instead.

It is an alias for *env erase*, described in :doc:`env`.

Example
-------

This uses sandbox, where the SPI flash location must be selected first since
the default is 'nowhere'::

    => env select SPIFlash
    Select Environment on SPIFlash: OK
    => saveenv
    Saving Environment to SPIFlash... SF: Detected m25p16 with page size 256 Bytes, erase size 64 KiB, total 2 MiB
    Erasing SPI flash...Writing to SPI flash...done
    Valid environment: 1
    OK
    => eraseenv
    Erasing Environment on SPIFlash... OK
    => env load
    Loading Environment from SPIFlash... *** Warning - bad CRC, using default environment

With the default 'nowhere' location there is nothing to erase::

    => eraseenv
    not possible

Configuration
-------------

The command is only available if CONFIG_CMD_ERASEENV=y, which in turn needs
CONFIG_CMD_SAVEENV=y.

Return value
------------

The return value $? is 0 (true) if the environment is erased. It is 1 (false)
if the location has no erase operation, is not initialised, or the erase
fails.
