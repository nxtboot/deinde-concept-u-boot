.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: pmic (command)

pmic command
============

Synopsis
--------

::

    pmic list
    pmic dev [name]
    pmic dump
    pmic read <reg>
    pmic write <reg> <value>

Description
-----------

The pmic command gives register-level access to a power management IC. It
works on the chip itself, below the supplies it provides, so it is the tool
for checking what a driver has actually written rather than what the
regulator uclass believes.

All the sub-commands except *list* act on a current device, which *dev*
selects. That choice is remembered until it is changed, so a session
normally starts with a *dev* and then reads or writes registers.

pmic list
~~~~~~~~~

This lists every device in the PMIC uclass, one per line, giving the device
name, its parent, the uclass and sequence number of that parent, and the
status returned when the device was probed. A non-zero status means the
device failed to probe and its registers cannot be reached.

pmic dev
~~~~~~~~

With a name this makes that device current; without one it reports which
device is current. The name is the device name, which is the left-hand
column of ``pmic list``.

name
    device name of the PMIC to make current

pmic dump
~~~~~~~~~

This reads every register of the current device and prints them 16 to a
line, each line labelled with the register number of its first entry. A
register which the driver reports as having no data is shown as dashes
rather than a value.

The width of each value follows the transfer length the driver declares, so
a chip with two-byte registers prints four hex digits per register.

pmic read
~~~~~~~~~

This reads one register and prints the register number and its value.

pmic write
~~~~~~~~~~

This writes one value to one register. Nothing is printed on success.

reg
    register number, in decimal or, with a 0x prefix, in hex

value
    value to write, in decimal or, with a 0x prefix, in hex

Both sub-commands check the register number against the count the driver
reports, so the last register which can be reached is one less than that
count.

Example
-------

This uses the sandbox PMIC, which sits on an emulated I2C bus::

    => pmic list
    | Name                            | Parent name         | Parent uclass @ seq
    | sandbox_pmic@40                 | i2c@0               | i2c @ 0 | status: 0
    | pmic@41                         | i2c@0               | i2c @ 0 | status: 0
    | pm8916@0                        | spmi@0              | spmi @ 0 | status: 0
    => pmic dev sandbox_pmic@40
    dev: 0 @ sandbox_pmic@40
    => pmic dump
    Dump pmic: sandbox_pmic@40 registers

    0x00: 08 00 00 2d 00 00 20 01 00 2d 00 00 00 00 00 00

A register can be read back after writing it::

    => pmic read 0
    0x00: 0x08
    => pmic write 0 0x33
    => pmic read 0
    0x00: 0x33

Asking for a device which is not there, or for a register beyond the end of
the chip, reports the error and the errno behind it::

    => pmic dev nosuchpmic
    Can't get PMIC: nosuchpmic!
    Error: -19 (No such device)
    => pmic read 0x99
    PMIC max reg: 16
    Error: -14 (Bad address)

Configuration
-------------

The pmic command is only available if CONFIG_CMD_PMIC=y.

Return value
------------

The return value $? is 0 (true) if the sub-command did its work, and 1
(false) if the device could not be found, if a register access failed, or
if the command line was not understood.

Asking for a sub-command with no current device set is treated as a usage
error, so the help text is printed as well as the message saying the device
has not been set.

See also
--------

* :doc:`clk<clk>` for the clocks such a chip may also provide
* :doc:`dm<dm>` for the driver-model devices behind these chips
* :doc:`regulator<regulator>` for the supplies a PMIC provides, at the level
  of volts rather than register values
