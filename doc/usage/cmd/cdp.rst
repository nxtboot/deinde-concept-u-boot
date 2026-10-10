.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: cdp (command)

cdp command
===========

Synopsis
--------

::

    cdp

Description
-----------

The cdp command announces the board on the wire and collects the VLAN settings
which the switch at the other end offers, using the Cisco Discovery Protocol.
It needs no address of its own, so it can run before the network is configured.

The command sends three announcements, 250ms apart, to the CDP multicast
address 01:00:0c:cc:cc:cc. Each one is an 802.2 packet carrying the SNAP header
Cisco uses for CDP. The command then reports what it has heard, so it always
takes about three quarters of a second whether or not the switch answers.

Two of the fields of a reply are read and the rest are ignored:

native VLAN
    the VLAN of untagged traffic on the port, stored in *nvlan*

appliance VLAN
    the VLAN the switch sets aside for a device such as a phone, stored in
    *vlan*

Both values are also given to the network stack, so a later network command
tags its packets with them. A field which the reply leaves out is not stored,
so the variable keeps whatever value it had.

Note that no announcement carries any information about the board. The device
ID, port ID, platform and version fields are built only when
CONFIG_CDP_DEVICE_ID and friends are defined, and no Kconfig option sets any of
them, so the packet holds nothing but a protocol version and a time-to-live. A
switch therefore learns that something is on the port but not what it is.

Example
-------

This is a board on a port offering both VLANs::

    => cdp
    Using eth@10002000 device
    CDP offered appliance VLAN 100
    CDP offered native VLAN 42
    => printenv vlan nvlan
    vlan=100
    nvlan=42

With nothing on the other end answering, the command says so::

    => cdp
    Using eth@10002000 device
    cdp failed; perhaps not a CISCO switch?

A reply which is too short to make sense of is reported and dropped, which
leaves the command with nothing to report::

    => cdp
    Using eth@10002000 device
    ** CDP packet is too short
    cdp failed; perhaps not a CISCO switch?

Return value
------------

The return value $? is set to 0 (true) if a reply arrived and to 1 (false) if
none did. A reply which holds neither VLAN still counts as a reply, so the
command can succeed without setting anything.

Configuration
-------------

The command is only available if CONFIG_CMD_CDP=y, which needs the legacy
network stack.

See also
--------

* :doc:`ethsw<ethsw>` for configuring an Ethernet switch which U-Boot drives
  itself, rather than asking one about its settings
* :doc:`bootp<bootp>` for obtaining an address once the VLAN is known
* :doc:`linklocal<linklocal>` for setting up the network with no server at all
* :doc:`dns<dns>` for looking up a host once the network is up
* *dhcp* for obtaining an address from a server instead
