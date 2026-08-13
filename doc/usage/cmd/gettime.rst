.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: gettime (command)

gettime command
===============

Synopsis
--------

::

    gettime

Description
-----------

The gettime command prints the value of U-Boot's millisecond timer, the one
returned by get_timer(). It takes no arguments.

Four lines are printed: the raw timer value, the same value split into whole
seconds and the remainder, and the number of timer ticks in a second. The tick
rate is CONFIG_SYS_HZ, which is 1000 on every board, so the raw value is a
count of milliseconds and the remainder is the milliseconds within the current
second.

On most boards the timer is set running early in start-up, so the value is
roughly the time U-Boot has been going. That is not guaranteed: the count comes
from whatever the timer driver provides, and on sandbox it comes from the host's
monotonic clock, so it is much larger than the time U-Boot has been running.

The value is held in a long, so on a 32-bit board it wraps after
2^32 / 1000 seconds, or about 49 days.

Example
-------

::

    => gettime
    Timer val: 2835770475
    Seconds : 2835770
    Remainder : 475
    sys_hz = 1000

Configuration
-------------

The gettime command is available if CONFIG_CMD_GETTIME=y.

Return value
------------

The return value $? is 0 (true) if the timer value is printed. It is 1 (false)
if any argument is given, since the command takes none.

See also
--------

* :doc:`bootstage<bootstage>` for timings of the individual steps of the boot,
  which is what the timer is usually wanted for
* :doc:`date<date>` for the time of day, which needs an RTC
* :doc:`sleep<sleep>` for waiting for a given time
* *time* for timing a single command
