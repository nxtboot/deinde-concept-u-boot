.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: bootp (command)

bootp command
=============

Synopsis
--------

::

    bootp [loadAddress] [[hostIPaddr:]bootfilename]

Description
-----------

The bootp command obtains the network settings of the board from a BOOTP
server and then, unless this is turned off, downloads the boot file the server
names over TFTP.

loadAddress
    memory address the file is loaded to. If not given, the address comes from
    the *loadaddr* environment variable

hostIPaddr
    address of the TFTP server to download from, in place of the *serverip*
    environment variable

bootfilename
    name of the file to download, in place of the name the server offers

A request is broadcast on the current Ethernet device and retried, with a
growing interval, until a server answers or the time limit is reached. That
limit is three seconds plus five for each retry CONFIG_NET_RETRY_COUNT allows,
so 28 seconds with the default of five, and the *bootpretryperiod*
environment variable overrides it.

The reply provides the address of the board and may carry the subnet mask, the
gateway, the name servers, the host name, the NIS domain and the root path as
well; each of those which arrives is written to the matching environment
variable (*ipaddr*, *netmask*, *gatewayip*, *dnsip*, *hostname*, *domain* and
*rootpath*).

The file is then downloaded over TFTP, as the *tftpboot* command would do, and
*filesize* and *fileaddr* are set to describe it. Setting the *autoload*
environment variable to 'no' stops this, so that the command obtains the
network settings alone. Where it is set to 'NFS' the file is fetched over NFS
instead.

Once the file is loaded the command boots it, as the *bootm* command would,
if the *autostart* environment variable is set to 'yes'.

Example
-------

This obtains the network settings and downloads the file named::

    => bootp 1000000 other.img
    BOOTP broadcast 1
    DHCP client bound to address 192.0.2.10 (0 ms)
    Using eth@10002000 device
    TFTP from server 192.0.2.2; our IP address is 192.0.2.10
    Filename 'other.img'.
    Load address: 0x1000000
    Loading: #
             22.5 KiB/s
    done
    Bytes transferred = 23 (17 hex)

With autoload turned off only the settings are obtained::

    => setenv autoload no
    => bootp
    BOOTP broadcast 1
    DHCP client bound to address 192.0.2.10 (0 ms)

Without a server to answer, the request is retried until the command gives
up::

    => bootp
    BOOTP broadcast 1
    BOOTP broadcast 2
    BOOTP broadcast 3
    BOOTP broadcast 4
    BOOTP broadcast 5
    BOOTP broadcast 6
    BOOTP broadcast 7

    Retry time exceeded; starting again

Return value
------------

The return value $? is set to 0 (true) if the network settings are obtained,
and to 1 (false) if no server answers, if the download fails or if the
arguments cannot be parsed.

Configuration
-------------

The command is only available if CONFIG_CMD_BOOTP=y and the legacy network
stack (CONFIG_NET_LEGACY=y) is in use.

With CONFIG_CMD_DHCP=y the request carries DHCP options and the DHCP state
machine handles the reply, which is why the address obtained is reported as a
DHCP one above. A plain BOOTP reply, holding no DHCP message type, is accepted
as it stands.

With CONFIG_BOOTP_SERVERIP=y the TFTP server is taken from the *serverip*
environment variable rather than from the reply. The boot file name in the
reply is then ignored as well, so name the file on the command line or set
*bootfile*; otherwise the download falls back to a name made up from the
address of the board, such as 'C000020A.img'.

See also
--------

* :doc:`tftpsrv<tftpsrv>` for receiving a file from a host instead of asking
  for one
* :doc:`tftpput<tftpput>` for sending a file to a server
* :doc:`dns<dns>` for looking up a host once the settings are in place
* :doc:`linklocal<linklocal>` for setting up the network with no server at all
* :doc:`wget<wget>` for downloading a file over HTTP
* *dhcp* for obtaining a lease from a DHCP server
* :doc:`rarpboot<rarpboot>` for obtaining the address of the board over RARP
* *tftpboot* for downloading a file over TFTP by hand
