.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: ext4size (command)

ext4size command
================

Synopsis
--------

::

    ext4size <interface> <dev[:part]> <filename>

Description
-----------

The ext4size command determines the size of a file on an ext2, ext3 or ext4
filesystem and stores it, in hexadecimal, in the environment variable
filesize. Nothing is printed on success.

One driver serves the whole ext family, so the name says which command was
enabled rather than which filesystem is on the device. There is no ext2size
command; use the filesystem-generic :doc:`size command <size>` where
CONFIG_CMD_EXT4 is not set.

If the file cannot be found, the filesize variable is left unchanged, so a
stale value from an earlier command may still be present. Check the return
value rather than assuming filesize describes the file just asked about.

The filesystem layer does not check that the path names a file, so a
directory succeeds and reports the size of its inode.

interface
    interface for accessing the block device (mmc, sata, scsi, usb, ....)

dev
    device number

part
    partition number, defaults to 0 (whole device)

filename
    path to file

Example
-------

This uses an ext4 image bound to the sandbox host interface, holding a file
hello.txt of 14 bytes and a directory docs containing note.txt::

    => ext4size host 0 hello.txt
    => echo $filesize
    e
    => ext4size host 0 /docs/note.txt
    => echo $filesize
    5

A missing file sets the return value but prints nothing, and leaves
filesize holding the size of the last file which was found::

    => ext4size host 0 missing.txt
    => echo $?
    1
    => echo $filesize
    5

A directory reports the size of its inode rather than failing::

    => ext4size host 0 /docs
    => echo $filesize
    1000

A device which is not there is reported by the block layer::

    => ext4size host 1 hello.txt
    ** Bad device specification host 1 **
    Couldn't find partition host 1

Configuration
-------------

The ext4size command is only available if CONFIG_CMD_EXT4=y.

Return value
------------

The return value $? is set to 0 (true) if the size was determined,
1 (false) otherwise.

See also
--------

* :doc:`ext4load<ext4load>` for reading the file into memory, which also
  sets filesize
* *ext4ls* for listing files with their sizes
* :doc:`size<size>` for the same operation on any supported filesystem
* :doc:`fatsize<fatsize>` for the same operation on a FAT filesystem
