.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: bootctl (command)

bootctl command
===============

Synopsis
--------

::

    bootctl list
    bootctl run

Description
-----------

The bootctl command provides access to boot control, an experimental subsystem
which discovers the Operating Systems available on a board and offers them in a
menu for the user to choose from. The pieces it needs - the logic which drives
the boot, the user interface, the lists of Operating Systems, the persistent
state and the measurement of what is loaded - are described in the devicetree,
as a 'boot schema', rather than being chosen at build time.

The command is not needed for boot control to work. It is a way of looking at
what the schema provides and of starting a boot by hand.

bootctl list
~~~~~~~~~~~~

This lists the logic drivers which are bound, one per line:

Seq
    sequence number of the device within its uclass

Name
    name of the device, which comes from its devicetree node

Type
    name of the uclass the device belongs to

Description
    what the driver does, as the driver itself reports it

Only the drivers which drive a boot are listed. The user-interface, OS-list,
state and measurement drivers which the same schema sets up belong to uclasses
of their own, so they do not appear here.

bootctl run
~~~~~~~~~~~

This performs a boot. The first logic driver is prepared, which locates the
user interface, the OS list and the state, then started, then polled until an
Operating System is chosen and booted. The user interface draws a menu of the
Operating Systems as they are found, so that one can be selected before the
scan finishes.

Where the chosen Operating System lives on an encrypted volume, the menu asks
for a passphrase and unlocks the volume before booting, optionally deriving the
key from a TKey.

The command does not return when a boot succeeds.

Example
-------

This shows the drivers available on sandbox::

    => bootctl list
    Seq  Name            Type            Description
    ---  --------------  --------------  --------------------
      0  bootctl         bootctrl        Controls the boot process
    ---  --------------  --------------  --------------------
    (1 driver)

Giving no sub-command, or one which is not recognised, prints the usage
message::

    => bootctl
    bootctl - Boot control

    Usage:
    bootctl list      - list bootctl drivers
    bootctl run       - run a boot

Return value
------------

The return value $? is 0 (true) for 'bootctl list', which cannot fail, and 1
(false) where no sub-command is given or the one given is not recognised.

For 'bootctl run' there is no success value to report, since the command does
not return when an Operating System starts. The return value is 1 (false)
where the boot cannot be set up or fails.

Configuration
-------------

The command is only available if CONFIG_CMD_BOOTCTL=y, which needs
CONFIG_BOOTCTL=y. That in turn needs CONFIG_EXPO=y for the user interface and
CONFIG_CMDLINE=y, and is enabled by default on sandbox and the EFI app.

See also
--------

* :doc:`bootflow<bootflow>` for listing and booting the Operating Systems
  which boot control offers in its menu, since its OS lists are built from
  bootflows
* :doc:`bootstd<bootstd>` for the standard-boot framework which finds those
  bootflows, whose bootdev order the schema's 'labels' option sets
* :doc:`bootmenu<bootmenu>` for a menu of boot options built from the
  environment rather than from a schema
* :doc:`tkey<tkey>` for the key device which boot control can use to unlock an
  encrypted volume
