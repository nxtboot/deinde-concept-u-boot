.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: fastboot (command)

fastboot command
================

Synopsis
--------

::

    fastboot [-l <addr>] [-s <size>] usb <controller>
    fastboot [-l <addr>] [-s <size>] udp
    fastboot [-l <addr>] [-s <size>] tcp

Description
-----------

The fastboot command turns the board into a fastboot device, so that a host
running the fastboot tool can download images to it, flash them to a partition
and boot them. It does not return until the host detaches, the transfer is
finished or the user presses Ctrl-C.

The protocol and the commands the board answers are described in
:doc:`../../android/fastboot`.

The transport is named by the first argument which is not an option. Leaving
it out selects usb, so 'fastboot 0' and 'fastboot usb 0' mean the same thing.
Only usb takes a controller number; udp and tcp listen on whichever network
device the ethact environment variable selects.

Each transport is built in separately, so a board may offer only some of them.
Asking for one which is not built in reports that and fails, rather than
falling back to another.

The download buffer holds the image the host sends. It defaults to
CONFIG_FASTBOOT_BUF_ADDR and CONFIG_FASTBOOT_BUF_SIZE, whose values the help
text shows, and the -l and -s options override them for the session which
follows. The buffer has to be large enough for the largest image the host
sends, since the protocol has no way to send one in pieces.

usb
    run as a USB gadget, downloading over the USB controller given. The number
    is the index of the controller, counted from 0, and must be given

udp
    listen for a host on UDP port CONFIG_UDP_FUNCTION_FASTBOOT_PORT (5554 by
    default)

tcp
    listen for a host on TCP port 5554

-l <addr>
    address of the download buffer, read as hexadecimal. It defaults to
    CONFIG_FASTBOOT_BUF_ADDR

-s <size>
    size of the download buffer in bytes, read as hexadecimal. It defaults to
    CONFIG_FASTBOOT_BUF_SIZE

Example
-------

Sandbox builds the command with the UDP transport alone, so the USB and TCP
examples below show what a board without those reports. Note that sandbox
shows the name of the function which logs a message, since it sets
CONFIG_LOGF_FUNC; a board without that setting prints the message by itself.

A transport is required::

    => fastboot
    fastboot - run as a fastboot usb or udp device

    Usage:
    fastboot [-l addr] [-s size] usb <controller> | udp
            addr - address of buffer used during data transfers (0x0)
            size - size of buffer used during data transfers (0x8192)
    => echo $?
    1

Asking for a transport which is not built in reports which one it is::

    => fastboot usb 0
         do_fastboot_usb() Fastboot USB not enabled
    => echo $?
    1

The usb word is optional, so this says the same thing::

    => fastboot 0
         do_fastboot_usb() Fastboot USB not enabled

and so does naming the TCP transport::

    => fastboot tcp
         do_fastboot_tcp() Fastboot TCP not enabled

An option needs a transport after it, since a lone '-' names none::

    => fastboot -
             do_fastboot() Error: Incorrect USB controller index
    fastboot - run as a fastboot usb or udp device

    Usage:
    fastboot [-l addr] [-s size] usb <controller> | udp
            addr - address of buffer used during data transfers (0x0)
            size - size of buffer used during data transfers (0x8192)

The UDP transport reports the device it listens on and the address a host
should use, then waits. Ctrl-C ends the wait::

    => fastboot udp
    Using eth@10002000 device
    Listening for fastboot command on 192.0.2.1

    Abort
    fastboot udp error: -4
    => echo $?
    1

Return value
------------

The return value $? is 0 (true) when the session ends of its own accord, for
instance when the host detaches or asks the board to continue booting.

It is 1 (false) when the transport named is not built in, when the USB
controller index is missing or malformed, when the gadget cannot be started or
when the wait is interrupted. An unrecognised option or a missing transport
produces the usage message, which is also a failure.

Configuration
-------------

The command is only available if CONFIG_CMD_FASTBOOT=y, which depends on
CONFIG_FASTBOOT=y.

Each transport is selected on its own:
CONFIG_USB_FUNCTION_FASTBOOT=y for usb, CONFIG_UDP_FUNCTION_FASTBOOT=y for udp
and CONFIG_TCP_FUNCTION_FASTBOOT=y for tcp, with the last two needing
CONFIG_NET_LEGACY=y as well. The command builds without any of them, in which
case every transport reports that it is not enabled.

CONFIG_CMD_FASTBOOT_ABORT_KEYED=y makes any key end a USB session, rather than
Ctrl-C alone.

See also
--------

* :doc:`../../android/fastboot` for the protocol and the commands a host may
  send
* :doc:`ums<ums>` for exporting a block device to a host over USB instead of
  downloading images from one
* :doc:`bootm<bootm>` for booting an image already in memory, which is what
  the fastboot boot command ends up doing
* :doc:`mmc<mmc>` for looking at the device the fastboot flash command writes
  to
* :doc:`gpt<gpt>` for writing a partition table by hand rather than through
  fastboot flash
* :doc:`../dfu` for updating firmware over USB using the DFU protocol instead
