.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: timer (command)

timer command
=============

Synopsis
--------

::

    timer start
    timer get

Description
-----------

The timer command is a stopwatch. 'timer start' notes the current value of
U-Boot's millisecond timer as the reference point and prints nothing; 'timer
get' prints how long it is since that point, in seconds with three decimal
places.

The reference point is kept in a static variable, so it survives from one
command to the next and can be read as often as wanted. It starts at 0, which
means 'timer get' without a preceding 'timer start' reports the time since the
timer itself started rather than a nonsense value.

There is only one reference point, so the command cannot time two things which
overlap.

The resolution is that of get_timer(), which is a millisecond on every board,
but the value covers the whole command line: it includes the time spent reading
and parsing the commands, not just running them.

Example
-------

Timing a delay::

    => timer start
    => sleep 2
    => timer get
    2.000

Reading the elapsed time again does not reset it, so the total keeps
growing::

    => sleep 3
    => timer get
    5.000

Configuration
-------------

The timer command is available if CONFIG_CMD_TIMER=y.

Return value
------------

The return value $? is 0 (true) when 'start' or 'get' is given. It is 1
(false) if there is no argument, or more than one. An argument which is
neither 'start' nor 'get' does nothing and still returns 0.

See also
--------

* :doc:`gettime<gettime>` for the raw timer value which this command works
  from
* :doc:`date<date>` for the time of day, which needs an RTC
* :doc:`sleep<sleep>` for waiting for a given time
* :doc:`bootstage<bootstage>` for timings of the individual steps of the boot
* *time* for timing a single command
