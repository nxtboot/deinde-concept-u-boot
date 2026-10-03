.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: mouse (command)

mouse command
=============

Synopsis
--------

::

    mouse dump

Description
-----------

The mouse command shows the events produced by a mouse, trackpad or
touchscreen. It is a way of checking that a pointing device is working within
U-Boot, since nothing else reports the events as they arrive.

Only the first mouse in the driver model tree is used, whether or not others
are present.

dump
    show each event as it arrives, until Ctrl-C is pressed

The command loops until Ctrl-C, so a device which produces no events simply
waits. On exit the number of events seen is printed, including the empty ones
which are counted but not shown.

A motion event reports where the pointer has moved to and how far it moved::

    motion: Xrel=<xrel>, Yrel=<yrel>, X=<x>, Y=<y>, but=<state>

xrel, yrel
    movement since the last event, which is negative for movement left or up

x, y
    position after the movement, in pixels from the top left of the display

state
    buttons held down during the movement, as a bitmask: 1 left, 2 middle,
    4 right, 8 scroll up, 16 scroll down

A button event reports a press or a release::

    button: button==<button>, press=<pressed>, clicks=<clicks>, X=<x>, Y=<y>

button
    which button changed, using the same numbering as the motion state

pressed
    1 for a press, 0 for a release

clicks
    number of clicks, so 2 for the second event of a double-click

x, y
    position of the pointer when the button changed

Example
-------

This shows the command on sandbox, where the mouse is driven by SDL and no
events arrive while the display is not in use::

    => mouse dump
    0 events received

Moving the mouse up and to the right while holding the left button produces a
motion event::

    motion: Xrel=10, Yrel=-20, X=100, Y=200, but=1

Double-clicking the right button produces a button event for each press and
release, the second of each pair reporting two clicks::

    button: button==4, press=1, clicks=2, X=150, Y=250

Where there is no mouse the command says so::

    => mouse dump
    Mouse not found (err=-19)

Configuration
-------------

The command is only available if CONFIG_CMD_MOUSE=y, which needs
CONFIG_MOUSE=y. Sandbox reads mouse events from SDL, so a display must be open
for any to arrive; the unit tests inject events through a back door instead.

Return value
------------

The return value $? is 0 (true) once the command is interrupted. It is
1 (false) if there is no mouse, or if reading from it fails.

A sub-command which is not dump, or no sub-command at all, gives the usage
message instead.

See also
--------

* :doc:`cedit<cedit>` for the configuration editor, which uses mouse clicks to
  move around its menus
* :doc:`button<button>` for reading the state of a board's buttons, the other
  sort of input device with a command of its own
* :doc:`video<video>` for the display the pointer position is measured against
