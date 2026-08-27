.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: regulator (command)

regulator command
=================

Synopsis
--------

::

    regulator list
    regulator dev [regulator-name]
    regulator info
    regulator status [-a]
    regulator value [val] [-f]
    regulator current [val]
    regulator mode [id]
    regulator enable
    regulator disable

Description
-----------

The regulator command inspects and adjusts the supplies which the driver
model knows about. It works in volts and amps, at the level the consumers of
a supply care about, rather than at the register level of the chip providing
it.

All the sub-commands except *list* act on a current regulator, which *dev*
selects. That choice is remembered until it is changed, so a session
normally starts with a *dev* and then reads or sets values.

Note that a regulator is named twice over. Its device name comes from the
devicetree node, and its regulator-name comes from the property of that
name. The sub-commands which take a name want the regulator-name, which is
the middle column of ``regulator list``.

regulator list
~~~~~~~~~~~~~~

This lists every device in the regulator uclass, giving the device name, the
regulator-name and the parent device. The devices are not probed, so the
list is available even where a supply cannot be reached.

regulator dev
~~~~~~~~~~~~~

With a regulator-name this makes that supply current; without one it reports
which supply is current, as its regulator-name and device name.

regulator-name
    the regulator-name of the supply to make current

regulator info
~~~~~~~~~~~~~~

This prints the constraints of the current supply: where it lives in the
device tree, the voltage and current limits it was given, whether it must
always be on or on at boot, and the operating modes it offers. These come
from the devicetree, so they say what the supply is allowed to do rather
than what it is doing.

regulator status
~~~~~~~~~~~~~~~~

With no option this prints what the current supply is doing: whether it is
enabled, its voltage, its current and its mode. A value the driver cannot
report is shown as the error behind it rather than as a number.

With -a it instead prints one line per supply, probing each one as it goes,
and ends each line with the status that probe returned. This is the quick
way to see the whole power tree, and it works whether or not a current
supply has been selected.

regulator value
~~~~~~~~~~~~~~~

With no argument this prints the voltage in microvolts. With a value it sets
it, refusing anything outside the constraints from the devicetree.

val
    voltage to set, in microvolts

-f
    set the voltage even though it is outside the constraints

The -f option comes after the value, not before it. A driver may still
refuse a forced value which its hardware cannot produce.

regulator current
~~~~~~~~~~~~~~~~~

With no argument this prints the current limit in microamps; with a value it
sets it, again within the constraints. There is no force option here.

val
    current limit to set, in microamps

regulator mode
~~~~~~~~~~~~~~

With no argument this prints the id of the operating mode; with an id it
changes to that mode. The ids and their names are those listed by
``regulator info``.

id
    id of the mode to select

regulator enable, regulator disable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These turn the output of the current supply on and off. Nothing is printed
on success.

Note that a supply whose type is fixed has nothing to adjust, so *value* and
*current* refuse to set one, although they will still read it, and *mode*
refuses it either way.

Example
-------

This uses the sandbox regulators, which hang off an emulated I2C PMIC::

    => regulator list
    | Device              | regulator-name                  | Parent
    | buck1               | SUPPLY_1.2V                     | sandbox_pmic@40
    | buck2               | SUPPLY_3.3V                     | sandbox_pmic@40
    | ldo1                | VDD_EMMC_1.8V                   | sandbox_pmic@40
    | ldo2                | VDD_LCD_3.3V                    | sandbox_pmic@40
    | no_match_by_nodename| buck_SUPPLY_1.5V                | sandbox_pmic@40
    | ldo3                | SUPPLY_1.8_3.3V                 | sandbox_pmic@40
    | pmbus@70            | sandbox-pmbus-vout              | i2c@0
    | reg@0               | sandbox-voltd0                  | protocol@17
    | reg@1               | sandbox-voltd1                  | protocol@17

The name to select with is the middle column::

    => regulator dev VDD_EMMC_1.8V
    dev: VDD_EMMC_1.8V @ ldo1
    => regulator info
    Regulator info:
    * regulator-name:  VDD_EMMC_1.8V
    * device name:     ldo1
    * parent name:     sandbox_pmic@40
    * parent uclass:   pmic
    * constraints:
      - min uV:        1800000
      - max uV:        1800000
      - min uA:        100000
      - max uA:        100000
      - always on:     0 (false)
      - boot on:       1 (true)
    * op modes:        4
      - mode id:       0 (OFF)
      - mode id:       1 (ON)
      - mode id:       2 (SLEEP)
      - mode id:       3 (STANDBY)
    => regulator status
    Regulator VDD_EMMC_1.8V status:
     * enable:         1 (true)
     * value uV:       1800000
     * current uA:     100000
     * mode id:        1 (ON)

A supply which cannot report a reading says so in place of the number::

    => regulator dev VDD_LCD_3.3V
    dev: VDD_LCD_3.3V @ ldo2
    => regulator current
    Regulator: VDD_LCD_3.3V - can't get the Current!
    Error: -38 (Function not implemented)

A voltage outside the constraints is refused, and -f overrides that::

    => regulator value 3400000
    Value exceeds regulator constraint limits 3300000..3300000 uV
    => echo $?
    1
    => regulator value 3400000 -f
    => regulator value
    3400000 uV

Configuration
-------------

The regulator command is only available if CONFIG_CMD_REGULATOR=y.

Return value
------------

The return value $? is 0 (true) if the sub-command did its work, and 1
(false) if the supply could not be found, if a value was outside the
constraints, if the driver refused the request, or if the command line was
not understood.

Note that ``regulator status -a`` reports the probe status of each supply in
its own column and still returns 0, so a supply which failed to probe does
not make the command fail.

See also
--------

* :doc:`pmic<pmic>` for the registers of the chip behind these supplies
* :doc:`clk<clk>` for the clocks such a chip may also provide
* :doc:`dm<dm>` for the driver-model devices these supplies are attached to
