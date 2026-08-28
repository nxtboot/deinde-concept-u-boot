.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: osd (command)

osd command
===========

Synopsis
--------

::

    osd show [<dev>]
    osd dev [<dev>]
    osd write <pos_x> <pos_y> <buffer> [<count>]
    osd print <pos_x> <pos_y> <color> <text>
    osd size <size_x> <size_y>

Description
-----------

The osd command drives an on-screen display: a character-based overlay drawn on
top of whatever a display is showing. It is a device of its own, separate from
the video console, so what it shows is not affected by :doc:`cls<cls>` or by
anything printed to the console.

The commands operate on the currently selected OSD, which is chosen with
'osd dev' and remembered until it is changed. Only 'osd show' works before one
has been selected.

osd show
~~~~~~~~

Show the OSD devices, each by its sequence number and name, marked '(active)'
if it is probed. With a number, show only that device.

osd dev
~~~~~~~

Show the currently selected OSD, or select the one to use for later commands.
Before anything is selected the current OSD is reported as -1.

osd write
~~~~~~~~~

Write raw data to the OSD memory at a given position. The data is given as hex
digits, two per byte, so it must have an even number of them. With a count, the
buffer is written that many times, one after another.

The layout of the memory is up to the driver. Both drivers in the tree use two
bytes per character cell, so a two-byte buffer with a count draws a run of
identical cells.

osd print
~~~~~~~~~

Write text to the OSD memory at a given position, in a colour the driver
understands. The sandbox driver takes 0 for black, 1 for white, 2 for red, 3
for green and 4 for blue, and refuses anything else.

osd size
~~~~~~~~

Set the size of the OSD in characters. The sandbox driver clears the contents
when it does this; a real one need not.

pos_x, pos_y
    position of the first character cell, hexadecimal, counted from the top
    left

count
    number of times to write the buffer, hexadecimal. Defaults to 1

color
    colour to draw the text in, hexadecimal. The meaning is driver-specific

size_x, size_y
    size of the display in characters, hexadecimal

Example
-------

Sandbox provides an emulated OSD, which starts out 10 characters square::

    => osd show
    OSD 0:	osd
    => osd dev
    No osd selected
    Current osd is -1
    => osd dev 0
    Setting osd to 0
    => osd show
    OSD 0:	osd  (active)

Drawing on it produces no output. Sandbox has no display to show it on, so the
result is visible only to the unit tests, which read the device memory back::

    => osd size 20 5
    => osd print 2 1 3 "OSD menu"
    => osd write 0 0 672d 12

The last command writes the cell 0x67 0x2d 18 times, drawing a line across the
top. The sandbox driver puts the colour first and records it as a letter, so
that is a green '-'.

An OSD which is not there, and a buffer which is not hex, are both reported::

    => osd dev 3
    Setting osd to 3
    cmd_osd_set_osd_num: No OSD 3 (err = -19)
    Failure changing osd number (err = -19)
    => osd write 0 0 zz
    Hexadecimal input contained invalid characters

Configuration
-------------

The osd command is available if CONFIG_CMD_OSD=y.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) if
no OSD is selected, if the device given to 'osd dev' does not exist, if the
buffer contains something other than hex digits, or if the driver rejects the
position, the colour or the size.

A missing argument, or a buffer with an odd number of hex digits, is reported
as a usage error, which is also 1 (false).

See also
--------

* :doc:`video<video>` for the display the OSD is drawn on top of
* :doc:`font<font>` for choosing the font used by the video console
* :doc:`cls<cls>` for clearing the video console, which leaves the OSD alone
