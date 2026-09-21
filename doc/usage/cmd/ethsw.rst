.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: ethsw (command)

ethsw command
=============

Synopsis
--------

::

    ethsw [port <port_no>] { enable | disable | show }
    ethsw [port <port_no>] statistics { [help] | [clear] }
    ethsw [port <port_no>] learning { [help] | show | auto | disable }
    ethsw [port <port_no>] [vlan <vid>] fdb { [help] | show | flush |
                                              { add | del } <mac> }
    ethsw [port <port_no>] pvid { [help] | show | <pvid> }
    ethsw [port <port_no>] vlan { [help] | show | add <vid> | del <vid> }
    ethsw [port <port_no>] untagged { [help] | show | all | none | pvid }
    ethsw [port <port_no>] egress tag { [help] | show | pvid | classified }
    ethsw vlan fdb { [help] | show | shared | private }
    ethsw [port <port_no>] ingress filtering
                           { [help] | show | enable | disable }
    ethsw [port <port_no>] aggr { [help] | show | <lag_group_no> }

Description
-----------

The ethsw command configures an Ethernet layer-2 switch: which ports carry
traffic, how each port tags and filters VLANs, what the switch has learned
about the stations attached to it and how its ports are aggregated.

The command line is a sequence of keywords rather than options. Each word is
matched against a fixed list, and the resulting sequence picks the function to
call. The optional 'port <port_no>' and 'vlan <vid>' keywords come first and
narrow what follows to one port or one VLAN; without them the whole switch is
addressed. A word which matches nothing, or a sequence which no sub-command
recognises, produces the usage message.

port_no
    port to act on, counted from 0. When it is left out the sub-command
    applies to every port

vid
    VLAN ID to act on. When it is left out the sub-command applies to every
    VLAN, except for fdb, where VID 1 is used

mac
    MAC address in the usual colon-separated form. The broadcast address is
    refused, since it is what the parser uses to mean 'no address given'

pvid
    port VLAN ID, used to tag an untagged frame arriving at the port

lag_group_no
    link-aggregation group the port belongs to

Each family of sub-commands prints its own help when it is given alone or
followed by 'help', so 'ethsw fdb help' describes the FDB sub-commands
without listing the rest.

ethsw enable, disable, show
~~~~~~~~~~~~~~~~~~~~~~~~~~~

These turn a port on or off and show its configuration.

ethsw statistics
~~~~~~~~~~~~~~~~

This shows the frame and byte counters of a port. Adding 'clear' resets them.

ethsw learning
~~~~~~~~~~~~~~

This shows the learning mode of a port, or sets it to automatic learning or
to none, so the switch stops adding entries to its filtering database for
frames arriving there.

ethsw fdb
~~~~~~~~~

This works on the filtering database, the table mapping a MAC address to the
port behind which it sits. Entries may be shown, flushed, or added and
deleted one at a time. An entry belongs to a VLAN, and VID 1 is used when no
'vlan <vid>' is given.

ethsw pvid
~~~~~~~~~~

This shows or sets the port VLAN ID, the VID given to a frame which arrives
at the port untagged.

ethsw vlan
~~~~~~~~~~

This shows which VLANs a port is a member of, and adds or deletes one.

ethsw untagged
~~~~~~~~~~~~~~

This sets which frames leave the port without a VLAN tag: all of them, none
of them, or only those whose VID matches the port's PVID.

ethsw egress tag
~~~~~~~~~~~~~~~~

This chooses where the VID in an egress tag comes from: the VID the frame was
classified with, or the PVID of the port.

ethsw vlan fdb
~~~~~~~~~~~~~~

This makes VLAN learning shared, so one filtering database serves every VLAN,
or private, so each VLAN has its own.

ethsw ingress filtering
~~~~~~~~~~~~~~~~~~~~~~~

This enables or disables VLAN ingress filtering on a port, which drops a
frame whose VID names a VLAN the port is not a member of.

ethsw aggr
~~~~~~~~~~

This shows or sets the link-aggregation group a port belongs to.

Example
-------

A switch driver registers the functions which carry these sub-commands out,
and no driver in the tree does so at present, so on any current board the
command parses the line and then reports that nothing can act on it. The
examples below therefore show the parsing rather than the effect.

A recognised sub-command reaches the switch, which is not there::

    => ethsw port 1 enable
    Command not available for: <NULL>
    => echo $?
    1

The name in that message is the one the driver gives itself, and is '<NULL>'
when no driver has registered.

A family given on its own prints its own help and succeeds::

    => ethsw fdb help
    ethsw [port <port_no>] [vlan <vid>] fdb { [help] | show | flush | { add | del } <mac> } - Add/delete a mac entry in FDB; use show to see FDB entries; if vlan <vid> is missing, VID 1 will be used
    => echo $?
    0

A MAC address which cannot be read says so, then prints the usage message::

    => ethsw fdb add zz
    Invalid MAC address: zz
    ethsw - Ethernet l2 switch commands

    Usage:
    ethsw [port <port_no>] { enable | disable | show } - enable/disable a port; show a port's configuration
    ...

The optional keywords cannot stand alone, since they say which port or VLAN
to act on but not what to do::

    => ethsw port 1
    ethsw - Ethernet l2 switch commands

    Usage:
    ...

Return value
------------

The return value $? is 0 (true) when a sub-command runs and reports success,
and 1 (false) when it fails, when the command line is not recognised, or when
the switch driver provides no function for the sub-command given.

Configuration
-------------

The command is only available if CONFIG_CMD_ETHSW=y.

See also
--------

* :doc:`dm<dm>` for listing the devices which are bound, which is how to see
  whether an Ethernet driver has probed
* :doc:`dns<dns>` for a network command which needs the link a switch port
  provides
* *mii* for reading and writing the registers of an Ethernet PHY
* *mdio* for reaching those registers through the driver-model MDIO uclass
