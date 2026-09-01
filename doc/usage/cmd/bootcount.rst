.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: bootcount (command)

bootcount command
=================

Synopsis
--------

::

    bootcount print
    bootcount reset

Description
-----------

The bootcount command reads and clears the boot counter, which U-Boot steps on
at every boot and which a board uses to notice that it keeps failing to reach a
working system.

Once the counter passes the value of the *bootlimit* environment variable,
U-Boot boots the command in *altbootcmd* rather than the one in *bootcmd*,
which is normally a recovery path of some kind. Something in that path is
expected to run 'bootcount reset', so that a system which does come up starts
counting again from zero.

print
    show the counter

reset
    set the counter back to zero. The *bootcount* environment variable is left
    alone, so the value from this boot is still there to look at

Where the counter is kept depends on the board: a scratch register in the RTC,
a byte of I2C EEPROM, a syscon register or the environment itself. All that
matters here is that it survives a reset.

A counter which has never been written reads as zero, so the command cannot
tell a fresh device from one whose counter is genuinely zero.

Example
-------

Sandbox keeps the counter in an emulated RTC, at offset 0x13 of the second RTC
device, as a byte of count behind the magic value 0xbc. This writes a count of
7 by hand and then shows the command reading it back and clearing it::

    => rtc dev 1
    RTC #1 - rtc@61
    => rtc write 13 07bc
    => bootcount print
    7
    => bootcount reset
    => bootcount print
    0

Configuration
-------------

The command is only available if CONFIG_CMD_BOOTCOUNT=y, which needs
CONFIG_BOOTCOUNT_LIMIT=y and a driver for wherever the counter is kept.

Return value
------------

The return value $? is 0 (true) for either sub-command. It is 1 (false) when no
sub-command is given, or when the one given is not recognised, in which case the
usage text is shown.

Note that 'bootcount print' says nothing about whether the counter could
actually be read: a device which is missing or whose contents make no sense
reports a count of zero and still succeeds.

See also
--------

* :doc:`bootd<bootd>` for running *bootcmd*, which is what the counter guards
* :doc:`reset<reset>` for the reset which steps the counter on
* :doc:`env<env>` for setting *bootlimit* and *altbootcmd*
