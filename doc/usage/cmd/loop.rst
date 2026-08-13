.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: loop (command)

loop command
============

Synopsis
--------

::

    loop [.b, .w, .l, .q] <address> <count>

Description
-----------

The loop command reads a range of memory over and over again, as fast as the
processor can manage. It is a bring-up tool: the repeated reads keep the address
and data lines busy, so the signals can be watched on an oscilloscope or a logic
analyser while a memory or a peripheral is checked over.

Nothing is printed and the values read are thrown away. Only the bus activity
matters.

**The command never returns.** It does not look for Ctrl-C, so there is no way
to stop it from the console; the board has to be reset. This is deliberate,
since a check for input in the middle of the loop would spoil the regular
pattern of accesses which the command exists to produce. Use the :doc:`mdc`
command instead when the values themselves are of interest, as that one does
stop on Ctrl-C.

When count is 1 the command uses a tighter loop which reads the same address
over and over, without the work of stepping through a range.

address
    address to read from, hexadecimal. Note that, unlike :doc:`md`, the offset
    set by the :doc:`base` command is *not* added to it

count
    number of values to read in each pass, hexadecimal. Note that this is not
    the same as the number of bytes, unless .b is used

The data_size suffix sets the size of each read (defaults to .l):

    =========  ===================
    data_size  Access size
    =========  ===================
    .b         byte
    .w         word (16 bits)
    .l         long (32 bits)
    .q         quadword (64 bits)
    =========  ===================

Example
-------

This reads sixteen bytes over and over. There is no output and no prompt comes
back::

    => loop.b 40000 10

Configuration
-------------

The loop command is available if CONFIG_CMD_MEMORY=y. Support for 64-bit
accesses (loop.q) is only available on 64-bit targets.

Return value
------------

The command does not return when it is given a valid command line. The return
value $? is 1 (false) if the address or the count is missing, or if the
data_size is not one of .b, .w, .l and .q.

See also
--------

* :doc:`loopw<loopw>` for writing a value to memory in the same way
* :doc:`md<md>` for displaying memory once
* :doc:`mdc<mdc>` for displaying memory repeatedly, which stops on Ctrl-C
* :doc:`mtest<mtest>` for checking that memory holds what is written to it
