.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: linklocal (command)

linklocal command
=================

Synopsis
--------

::

    linklocal

Description
-----------

The linklocal command gives the board an IPv4 address on the 169.254.0.0/16
link-local network, following RFC 3927, so that it can talk to other machines
on the same wire with no DHCP server present.

The command picks an address, sends ARP probes to see whether any other
machine is using it and, when none answers, announces it as its own. An
address which is already taken is dropped and another is tried. The whole
exchange takes about ten seconds, since the protocol waits between probes.

The address is chosen from the value of *llipaddr*, if that variable holds one
in 169.254.0.0/16, so that a board keeps the address it had last time. If the
variable is empty the address is picked at random, seeded from the MAC address
of the interface, and an address outside the link-local range is refused.

On success these environment variables are set:

ipaddr
    the address which has been assigned

llipaddr
    the same address, so that the next run of the command reuses it

netmask
    255.255.0.0, the size of the link-local network

gatewayip
    0.0.0.0, since a link-local network has no router

Example
-------

This picks an address and shows what is left in the environment::

    => linklocal
    Successfully assigned 169.254.195.225
    => printenv ipaddr netmask gatewayip llipaddr
    ipaddr=169.254.195.225
    netmask=255.255.0.0
    gatewayip=0.0.0.0
    llipaddr=169.254.195.225

An address outside the link-local range cannot be claimed::

    => setenv llipaddr 192.0.2.7
    => linklocal
    invalid link address

Return value
------------

The return value $? is set to 0 (true) if an address was assigned and to 1
(false) if it was not, either because *llipaddr* is outside the link-local
range or because no free address could be found.

Configuration
-------------

The command is only available if CONFIG_CMD_LINK_LOCAL=y, which needs the
legacy network stack and a source of random numbers.

See also
--------

* *dhcp* for obtaining an address from a server instead
* :doc:`dns<dns>` for looking up a host once the network is up
* *ping* for checking that the address works
