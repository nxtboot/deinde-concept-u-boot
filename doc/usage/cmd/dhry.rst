.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: dhry (command)

dhry command
============

Synopsis
--------

::

    dhry [iterations]

Description
-----------

The dhry command runs the Dhrystone 2.1 benchmark, which gives a rough measure
of integer CPU performance. It is an old public-domain benchmark, so the
numbers it produces are comparable with figures published for a great many
machines, but it exercises only the CPU and its caches: nothing in the result
depends on the board's memory bandwidth, peripherals or clock configuration.

iterations
    number of times to run the benchmark loop, in decimal. The default is
    1000000.

The command reports how long the run took, how many iterations that works out
to per second, and the same figure expressed in Dhrystone MIPS. A DMIPS is the
rate divided by 1757, which is what a VAX 11/780 managed, that machine being
the one DMIPS figures have always been quoted against.

The duration is measured to the millisecond, so a run which finishes inside one
millisecond cannot be timed. The command says so and returns failure rather
than reporting a rate; ask for more iterations in that case.

Nothing about the result is saved, so a script which wants the figure must
parse the output.

Example
-------

This runs the benchmark with its default iteration count::

    => dhry
    1000000 iterations in 23 ms: 43478260/s, 24745 DMIPS

This asks for too few iterations to time::

    => dhry 1
    1 iterations take under 1ms: try more
    => echo $?
    1

Configuration
-------------

The command is available when CONFIG_CMD_DHRYSTONE=y.

Return value
------------

The return value $? is 0 (true) if the run was long enough to time and 1
(false) if it was not, or if more than one argument is given.

See also
--------

* :doc:`timer<timer>` for measuring an interval by hand, which is how to time
  something this command does not cover
* :doc:`gettime<gettime>` for the raw timer value, the same one the benchmark
  measures itself against
* :doc:`cpu<cpu>` for what the CPU says about itself, including the clock speed
  the result should be read against
* :doc:`mtest<mtest>` for exercising memory rather than the CPU
