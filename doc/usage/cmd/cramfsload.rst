.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: cramfsload (command)

cramfsload command
==================

Synopsis
--------

::

    cramfsload [<addr> [<filename>]]

Description
-----------

The cramfsload command reads a file out of a CRAMFS image which is already in
memory, and writes it to another address. The image itself is not on a block
device: the address it sits at comes from the *cramfsaddr* environment
variable, which must be set before the command is used.

addr
    address to load the file to, hexadecimal. Defaults to the standard load
    address, which is the value of *loadaddr* where that is set. Note that
    giving an address also changes the standard load address for later
    commands.

filename
    path of the file within the image, without a leading '/'. Defaults to the
    value of the *bootfile* environment variable, or to 'uImage' where that is
    not set.

The number of bytes read is saved in the environment variable *filesize*.

Only files can be read; there is nothing to select a subvolume or a partition,
since a CRAMFS image holds a single read-only filesystem.

Example
-------

Read a file from an image which has been loaded to 0x1000000::

    => setenv cramfsaddr 1000000
    => cramfsls
        -rw-r--r--            14 hello.txt
        drwxr-xr-x            76 subdir

    2 file(s), 1 dir(s)

    => cramfsload 2000000 hello.txt
    ### CRAMFS load complete: 14 bytes loaded to 0x2000000
    => printenv filesize
    filesize=e

A file in a subdirectory is named by its path within the image::

    => cramfsload 2000000 subdir/deep.txt
    ### CRAMFS load complete: 10 bytes loaded to 0x2000000

Asking for a file which the image does not hold reports the failure twice, once
from the filesystem code and once from the command::

    => cramfsload 2000000 nofile.txt
    can't find corresponding entry
    ### CRAMFS LOAD ERROR<0> for nofile.txt!
    => echo $?
    1

Return value
------------

The return value $? is 0 (true) if the file is read. It is 1 (false) if
*cramfsaddr* is not set, if there is no CRAMFS image at that address, or if the
file is not in the image.

See also
--------

* :doc:`cramfsls<cramfsls>` for listing what the image holds
* :doc:`load<load>` for reading a file from a filesystem on a block device
  rather than from an image in memory
* *unzip* for expanding a compressed region of memory, which is the other way
  of getting data out of an image already in RAM
