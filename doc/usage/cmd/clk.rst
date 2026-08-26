.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: clk (command)

clk command
===========

Synopsis
--------

::

    clk dump
    clk setfreq <clk> <freq>

Description
-----------

The clk command inspects and adjusts the clocks which the driver model knows
about. It works on the clock devices themselves, not on the consumers which
ask them for a rate, so it shows the tree as the clock drivers see it.

clk dump
~~~~~~~~

This walks every device in the clock uclass, probing each one, and prints a
line per clock giving its rate in Hz, the number of consumers which have
enabled it, and its name. Children are indented under their parent, so the
shape of the tree is visible at a glance.

A clock which already has a parent is skipped at the top level, so it is
printed under that parent rather than a second time on its own.

A driver may also provide its own dump method. Where it does, its output is
printed after the tree, under a heading naming the driver and the device.

clk setfreq
~~~~~~~~~~~

This asks a clock to change its rate. The clock is named by its device name,
which is the name in the left-hand column of ``clk dump``, and the frequency
is given in Hz as a decimal number.

clk
    device name of the clock to change

freq
    new rate in Hz

The value printed is whatever the driver returns from its ``set_rate()``
method. Drivers differ in what that means: the rate which was actually set,
or, as with sandbox, the rate which was in force before the change. Read the
new rate back with ``clk dump`` rather than trusting the printed number.

Example
-------

This is the clock tree of the sandbox test devicetree::

    => clk dump
     Rate               Usecnt      Name
    ------------------------------------------
     321                  0        |-- clk-sbox
     1234                 0        |-- clk-fixed
     20000000             0        |-- osc
     480000000            0        |   `-- pll3_usb_otg
     60000000             0        |       |-- pll3_60m
     20000000             0        |       |   |-- ecspi_root
     20000000             0        |       |   |   |-- ecspi0
     20000000             0        |       |   |   `-- ecspi1
     60000000             0        |       |   |-- usdhc1_sel
     60000000             0        |       |   `-- i2c
     60000000             0        |       |       `-- i2c_root
     80000000             0        |       `-- pll3_80m
     80000000             0        |           `-- usdhc2_sel
     333                  0        |-- scmi-0
     200                  0        |-- scmi-1
     1000                 0        `-- scmi-2

Changing a rate reports 321, the rate which was in force beforehand, and the
new rate is then visible in the tree::

    => clk setfreq clk-sbox 500
    set_rate returns 321
    => clk dump
     Rate               Usecnt      Name
    ------------------------------------------
     500                  0        |-- clk-sbox
    ...

A clock which is not there is reported by name::

    => clk setfreq nosuchclock 500
    clock 'nosuchclock' not found.
    => echo $?
    1

Configuration
-------------

The clk command is only available if CONFIG_CMD_CLK=y.

Return value
------------

The return value $? is 0 (true) if the sub-command did its work, and 1
(false) if the clock could not be found, if ``set_rate()`` failed, or if the
command line was not understood.

Note that a rate outside what the hardware can do is not necessarily an
error: a driver is free to round the request to something it can produce and
report success.

See also
--------

* :doc:`dm<dm>` for the driver-model devices behind these clocks, which
  ``dm uclass`` lists by uclass
* :doc:`scmi<scmi>` for the SCMI server which provides the scmi-* clocks
* *pmic* for the register-level view of a power management chip
* *regulator* for the supplies such a chip provides
