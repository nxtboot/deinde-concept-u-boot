.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: fatrm (command)

fatrm command
=============

Synopsis
--------

::

    fatrm <interface> <dev[:part]> <filename>

Description
-----------

The fatrm command deletes a file from a FAT filesystem. Nothing is printed on
success. Note that you can use the :doc:`rm command <rm>` instead.

An empty directory can be deleted as well, so fatrm doubles as the counterpart
of :doc:`fatmkdir<fatmkdir>`. A directory which still holds entries other than
. and .. is refused.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

filename
    path to the file or empty directory to delete

Example
-------

This uses a FAT image bound to the sandbox host interface. The directory can
only go once the file inside it has::

    => fatrm host 0 docs
    Error: directory is not empty: 3
    => fatrm host 0 docs/note.bin
    => fatrm host 0 docs
    => fatls host 0
    0 file(s), 0 dir(s)

Deleting something which is not there fails::

    => fatrm host 0 missing.bin
              fat_unlink() missing.bin: doesn't exist (-2)

Configuration
-------------

The fatrm command is only available if CONFIG_CMD_FAT=y and
CONFIG_FAT_WRITE=y.

Return value
------------

The return value $? is set to 0 (true) if the file or directory was deleted,
1 (false) otherwise.

See also
--------

* :doc:`fatmkdir<fatmkdir>` for creating the directories fatrm removes
* :doc:`fatwrite<fatwrite>` for creating the files fatrm removes
* :doc:`rm<rm>` for the same operation on any supported filesystem
* *fatls* for checking what is left afterwards
