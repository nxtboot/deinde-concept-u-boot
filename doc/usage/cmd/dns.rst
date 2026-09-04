.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: dns (command)

dns command
===========

Synopsis
--------

::

    dns hostname [envvar]

Description
-----------

The dns command looks up the address of a host by asking a name server for it.
The address of the name server must be in the *dnsip* environment variable,
either set by hand or obtained from a DHCP server.

The address which comes back is printed. It is also stored in the *envvar*
environment variable, if one is named, so that a later command can use it.

hostname
    name of the host to look up, at most 254 characters

envvar
    environment variable to hold the address which is found

Only the first address of the response is used and only IPv4 (A) records are
understood, so a host which has an IPv6 address alone cannot be found. A
response holding no address at all is reported as a lookup which worked, since
the name server did answer.

Example
-------

This looks up a host and then uses the address it finds::

    => setenv dnsip 192.0.2.2
    => dns u-boot.example.com
    192.0.2.99
    => dns u-boot.example.com hostip
    192.0.2.99
    => printenv hostip
    hostip=192.0.2.99

Without a name server the lookup cannot start::

    => setenv dnsip
    => dns u-boot.example.com
    *** ERROR: DNS server address not given
    dns lookup of u-boot.example.com failed, check setup

A name the server does not know is reported like this::

    => dns nosuchhost.example.com
    DNS: host not found

Return value
------------

The return value $? is set to 0 (true) if the name server answers, even when
its answer holds no address, and to 1 (false) if there is no name server to
ask or it does not answer.

Configuration
-------------

The command is only available if CONFIG_CMD_DNS=y.

Both network stacks provide it, from different code: the legacy stack
(CONFIG_NET_LEGACY=y) has its own resolver, while with CONFIG_NET_LWIP=y the
lwIP one is used. The lwIP version prints the address only when no environment
variable is named, and treats a name which does not resolve as a failure.

See also
--------

* :doc:`sntp<sntp>` for setting the time from a network server
* :doc:`wget<wget>` for downloading a file over HTTP
* *dhcp* for obtaining the name-server address automatically
* *ping* for checking that a host answers
