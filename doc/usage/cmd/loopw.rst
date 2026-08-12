.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: loopw (command)

loopw command
=============

Synopsis
--------

::

    loopw [.b, .w, .l, .q] <address> <count> <value>

Description
-----------

The loopw command writes a value to a range of memory over and over again, as
fast as the processor can manage. Like :doc:`loop` it is a bring-up tool, the
difference being that the bus carries a known value rather than whatever the
memory happens to hold, which makes the data lines easier to follow on an
oscilloscope or a logic analyser.

The same value is written to every address in the range, so the range is left
holding that value once the board is reset.

**The command never returns.** It does not look for Ctrl-C, so there is no way
to stop it from the console; the board has to be reset. This is deliberate,
since a check for input in the middle of the loop would spoil the regular
pattern of accesses which the command exists to produce. Use the :doc:`mwc`
command instead when the writes need to stop, as that one waits for a delay
between writes and stops on Ctrl-C.

When count is 1 the command uses a tighter loop which writes to the same address
over and over, without the work of stepping through a range.

address
    address to write to, hexadecimal. Note that, unlike *mw*, the offset set by
    the :doc:`base` command is *not* added to it

count
    number of values to write in each pass, hexadecimal. Note that this is not
    the same as the number of bytes, unless .b is used

value
    value to write, hexadecimal

The data_size suffix sets the size of each write (defaults to .l):

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

This writes 0xaa to each of sixteen bytes over and over. There is no output and
no prompt comes back::

    => loopw.b 40000 10 aa

Configuration
-------------

The loopw command is available if CONFIG_LOOPW=y. Support for 64-bit accesses
(loopw.q) is only available on 64-bit targets.

Return value
------------

The command does not return when it is given a valid command line. The return
value $? is 1 (false) if the address, the count or the value is missing, or if
the data_size is not one of .b, .w, .l and .q.

See also
--------

* :doc:`loop<loop>` for reading memory in the same way
* *mw* for writing a value to memory once, or filling a region
* :doc:`mwc<mwc>` for writing to memory repeatedly, which stops on Ctrl-C
* :doc:`mtest<mtest>` for checking that memory holds what is written to it
