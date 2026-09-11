.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: pmc (command)

pmc command
===========

Synopsis
--------

::

    pmc init
    pmc info

Description
-----------

The pmc command reads the state of the Intel Power-Management Controller and
shows what it holds. The PMC records why the machine woke up, which General
Purpose Events are enabled, and how the watchdog and the general
power-management registers are set, so it is the place to look when a board
resumes from the wrong state or reboots on its own.

The state is read once, into memory held by the driver, rather than each
register being read as it is printed.

init
    read the state from the controller and stop, printing nothing

info
    read the state and then show it

Both sub-commands read the state, so info alone is enough to see the current
values; init is useful when the state is wanted for a later command without
the output.

The fields are:

Device
    name of the controller in the driver model tree

ACPI base
    I/O address of the ACPI register block, from which the PM1 and GPE0
    registers are read

pmc_bar0, pmc_bar2
    addresses of the two memory-mapped register regions of the controller

gpe_cfg
    address of the GPE configuration register

pm1_sts, pm1_en, pm1_cnt
    PM1 status, enable and control registers, which record the sleep state
    the machine woke from

gpe0_sts, gpe0_en
    status and enable values for each of the four GPE0 register banks

prsts
    power and reset status

tco_sts
    the two status registers of the TCO watchdog

gen_pmcon1, gen_pmcon2, gen_pmcon3
    the general power-management configuration registers

Example
-------

This shows the state of the emulated controller in sandbox::

    => pmc info
    Device: pci@1e,0
    ACPI base 0, pmc_bar0 000000001695c410, pmc_bar2 0000000000000000, gpe_cfg 000000001695d460
    pm1_sts: 0000 pm1_en: 0002 pm1_cnt: 00000004
    gpe0_sts[0]: 00000020 gpe0_en[0]: 00000030
    gpe0_sts[1]: 00000024 gpe0_en[1]: 00000034
    gpe0_sts[2]: 00000028 gpe0_en[2]: 00000038
    gpe0_sts[3]: 0000002c gpe0_en[3]: 0000003c
    prsts: 00000000
    tco_sts:   0064 0066
    gen_pmcon1: 00000000 gen_pmcon2: 00000000 gen_pmcon3: 00000000

The addresses shown for pmc_bar0 and gpe_cfg are where the registers appear in
this run, so they differ from one run to the next.

Reading the state without showing it prints nothing::

    => pmc init
    =>

Return value
------------

The return value $? is 0 (true) if the state is read. It is 1 (false) if there
is no power-management controller, or if the controller cannot be read.

A sub-command which is not init or info, or no sub-command at all, gives the
usage message instead.

Configuration
-------------

The command is only available if CONFIG_CMD_PMC=y, which needs
CONFIG_ACPI_PMC=y. Sandbox uses an emulated controller behind its emulated PCI
bus, so the values above are made up rather than read from real hardware.

See also
--------

* :doc:`acpi<acpi>` for the ACPI tables which describe this hardware to the
  operating system
* :doc:`iod<iod>` for reading the I/O space the PM1 and GPE0 registers live in
* :doc:`msr<msr>` for reading the x86 model-specific registers, another place
  where power settings are kept
