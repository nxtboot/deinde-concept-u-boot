.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: rarpboot (command)

rarpboot command
================

Synopsis
--------

::

    rarpboot [loadAddress] [[hostIPaddr:]bootfilename]

Description
-----------

The rarpboot command obtains the address of the board from a RARP server and
then, unless this is turned off, downloads a boot file over TFTP.

loadAddress
    memory address the file is loaded to. If not given, the address comes from
    the *loadaddr* environment variable

hostIPaddr
    address of the TFTP server to download from, in place of the *serverip*
    environment variable

bootfilename
    name of the file to download, in place of the *bootfile* environment
    variable

A request is broadcast on the current Ethernet device and retried every five
seconds until a server answers or CONFIG_NET_RETRY_COUNT attempts have been
made. The reply carries the address of the board, which is written to the
*ipaddr* environment variable, and the address of the server, which is used
for the download when *serverip* is not set already.

RARP is the oldest of the three ways U-Boot has of asking for an address and
the least useful: a reply holds the addresses alone, so there is nothing to
fill in the subnet mask, the gateway or the boot file name, and a RARP server
must see the request on the local link. Prefer *dhcp* or
:doc:`bootp<bootp>` unless the server at hand speaks RARP alone.

The file is then downloaded over TFTP, as the *tftpboot* command would do, and
*filesize* and *fileaddr* are set to describe it. The name comes from the
command line or from *bootfile*, since the reply carries none; with neither,
the download falls back to a name made up from the address of the board, such
as 'C000020A.img'. Setting the *autoload* environment variable to 'no' stops
this step, so that the command obtains the address alone. Where it is set to
'NFS' the file is fetched over NFS instead.

Once the file is loaded the command boots it, as the *bootm* command would,
if the *autostart* environment variable is set to 'yes'.

Example
-------

This obtains the address of the board with autoload turned off::

    => setenv autoload no
    => rarpboot
    RARP broadcast 1
    => printenv ipaddr
    ipaddr=192.0.2.10

A reply which is not a RARP reply, or is too short to be one, is reported and
the request stands until another arrives::

    => rarpboot
    RARP broadcast 1
    invalid RARP header

Without a server to answer, the request is retried until the command gives
up::

    => rarpboot
    RARP broadcast 1
    RARP broadcast 2
    RARP broadcast 3
    RARP broadcast 4
    RARP broadcast 5

    Retry count exceeded; starting again

Return value
------------

The return value $? is set to 0 (true) if the address is obtained, and to 1
(false) if no server answers, if the download fails or if the arguments
cannot be parsed.

Configuration
-------------

The command is only available if CONFIG_CMD_RARP=y and the legacy network
stack (CONFIG_NET_LEGACY=y) is in use. The lwIP stack has no RARP support.

CONFIG_NET_RETRY_COUNT sets how many requests are sent before the command
gives up, five by default.

See also
--------

* :doc:`bootp<bootp>` for obtaining the subnet mask and the boot file name as
  well as the address
* :doc:`linklocal<linklocal>` for choosing an address with no server at all
* *dhcp* for obtaining a lease from a DHCP server
* *tftpboot* for downloading a file over TFTP by hand
