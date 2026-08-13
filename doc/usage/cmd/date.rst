.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: date (command)

date command
============

Synopsis
--------

::

    date
    date <MMDDhhmm[[CC]YY][.ss]>
    date reset

Description
-----------

The date command shows and sets the time of day held by the board's real-time
clock. The clock is the first RTC device in the system, so a board with more
than one can only reach the others with the *rtc* command.

With no argument the command reads the clock and prints the date, the day of
the week and the time. The day of the week comes from the RTC itself, not from
the date, so a clock which does not keep it prints 'unknown day'.

With a numeric argument the clock is set. The argument is a run of digits with
no separators:

MM
    month, 01 to 12

DD
    day of the month, 01 to the number of days in that month

hh
    hour, 00 to 23

mm
    minute, 00 to 59

CC
    century, so 20 for the years 2000 to 2099. It can only be given along with
    the year

YY
    year within the century. Without it the year already in the clock is kept

.ss
    seconds, 00 to 59. Without it the seconds are set to 0

The day of the week is worked out from the date given, so it is never entered.
Only the lengths 8 (MMDDhhmm), 10 (MMDDhhmmYY) and 12 (MMDDhhmmCCYY) are
accepted, each with an optional '.ss' on the end; anything else is a bad date
format.

With the 'reset' argument the RTC is reset and then set to midnight on
1 January 2000. This is for a clock which has stopped or has never been set,
where the values in its registers may not be a valid date at all.

After setting the clock, in either form, the command reads it back and prints
it, so the result can be checked.

Example
-------

Reading the clock::

    => date
    Date: 2026-08-13 (Thursday)    Time:  9:37:01

Setting it to 12:00:30 on 25 December 2026. The century and year are given, so
the date is fully specified::

    => date 122512002026.30
    Date: 2026-12-25 (Friday)    Time: 12:00:30

The date stays set and can be read back later::

    => date
    Date: 2026-12-25 (Friday)    Time: 12:00:30

Resetting the clock::

    => date reset
    Reset RTC...
    Date: 2000-01-01 (Saturday)    Time:  0:00:00

A string which is not one of the accepted lengths is refused and the clock is
left alone::

    => date 99
    ## Bad date format
    => echo $?
    1

Configuration
-------------

The date command is available if CONFIG_CMD_DATE=y, which is the default when
CONFIG_DM_RTC=y.

Return value
------------

The return value $? is 0 (true) if the date is shown or set. It is 1 (false)
if no RTC device can be found, if the clock cannot be read or written, if the
date format is bad, or if more than one argument is given.

See also
--------

* *rtc* for reading and writing individual RTC registers, and for reaching an
  RTC other than the first
* :doc:`sntp<sntp>` for setting the clock from a time server on the network
* :doc:`gettime<gettime>` for the time since U-Boot started, which does not
  need an RTC
* :doc:`timer<timer>` for measuring an interval rather than reading the time
  of day
