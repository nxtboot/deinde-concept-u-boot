.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: demo (command)

demo command
============

Synopsis
--------

::

    demo list
    demo hello <num> [<char>]
    demo light <num> [<value>]
    demo status <num>

Description
-----------

The demo command drives the demo uclass, which exists to show how driver model
works and to give its tests something to operate on. It does nothing useful:
each device says hello, draws a shape, keeps a count of the characters it has
drawn and pretends to have a set of lights. See
:doc:`../../develop/driver-model/design` for a walk through the code behind it.

Two drivers implement the uclass. The simple one prints a line for hello and
supports nothing else; the shape one draws a shape whose number of sides comes
from its platform data, reports the number of characters drawn as its status,
and drives the lights through the GPIOs its devicetree node names, where it has
one.

Every sub-command apart from 'demo list' takes the number of the device to
operate on, in decimal. That is the position of the device in the uclass, as
shown by 'demo list', not a sequence number.

demo list
~~~~~~~~~

List the devices in the demo uclass, with the address of each device, of its
operations and of its platform data, and the error from probing it, which is 0
when the device is ready to use.

demo hello
~~~~~~~~~~

Say hello. The shape driver draws a shape from the character given, or from the
one in its platform data when none is, and the simple driver prints a line
naming its colour and number of sides.

demo light
~~~~~~~~~~

Show the state of the lights, or set it. A device with no light GPIOs reports 0
and ignores what it is given.

demo status
~~~~~~~~~~~

Show the number of characters the device has drawn so far. Only the shape
driver keeps a count.

num
    number of the demo device, decimal

char
    character to draw the shape with, taken from the platform data when it is
    not given

value
    bit mask of the lights to turn on, hexadecimal

Example
-------

Sandbox binds five demo devices from platform data, three of them shape devices
and two simple ones::

    => demo list
    Demo uclass entries:
    entry 0 - instance 0788f860, ops ff000001, plat ff000000, status 0
    entry 1 - instance 0788f9b0, ops ff000002, plat ff000000, status 0
    entry 2 - instance 0788fb00, ops ff000001, plat ff000003, status 0
    entry 3 - instance 0788fc50, ops ff000002, plat ff000004, status 0
    entry 4 - instance 0788fda0, ops ff000001, plat ff000004, status 0

Device 0 is a four-sided shape drawn in red. Its status counts the characters
it draws, so it starts at zero and grows by 48 for each shape, and
'demo hello 0 x' draws the same shape with a different character::

    => demo status 0
    Status: 0
    => demo hello 0
    r@@@@@@@
    e@@@@@@@
    d@@@@@@@
    r@@@@@@@
    e@@@@@@@
    d@@@@@@@
    => demo status 0
    Status: 48
    => demo hello 0 x
    rxxxxxxx
    exxxxxxx
    dxxxxxxx
    rxxxxxxx
    exxxxxxx
    dxxxxxxx

None of these devices has light GPIOs, so the lights always read 0::

    => demo light 0
    Light: 0
    => demo light 0 3
    => demo light 0
    Light: 0

Device 1 is a simple device, which says hello and refuses everything else::

    => demo hello 1
    Hello from 0788f9b0: red 4
    => demo status 1
    Command 'status' failed: Error -38

A device which is not there is reported in the same way::

    => demo hello 9
    Command 'hello' failed: Error -19

Configuration
-------------

The demo command is available if CONFIG_CMD_DEMO=y. The uclass itself needs
CONFIG_DM_DEMO=y, with CONFIG_DM_DEMO_SIMPLE=y and CONFIG_DM_DEMO_SHAPE=y for
the two drivers.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) if
the device does not exist or if its driver does not implement the operation.

A missing device number, or a sub-command which does not exist, is reported as
a usage error, which is also 1 (false).

See also
--------

* :doc:`dm<dm>` for the devices and uclasses driver model has bound
* :doc:`bind<bind>` for attaching a driver to a device by hand
* :doc:`unbind<unbind>` for detaching one again
