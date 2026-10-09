.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: bootvx (command)

bootvx command
==============

Synopsis
--------

::

    bootvx [<addr>]
    bootvx tftp

Description
-----------

The bootvx command starts a VxWorks image which is already in memory. The
image may be an ELF executable, in which case its program headers say where
each segment belongs, or a raw binary, in which case the command jumps
straight to the address it is given.

Before starting the image the command assembles the *bootline*, the string
VxWorks reads to learn where it should look for its file system and how its
network is configured. The bootaddr environment variable says where to put
it, which is a board-specific address the VxWorks BSP agrees on;
LOCAL_MEM_LOCAL_ADRS plus BOOT_LINE_OFFSET in BSP terms, which is 0x4200 on
PowerPC. Without bootaddr the command has nowhere to write and fails.

The bootline is the value of the bootargs environment variable where that is
set. Otherwise the command builds one from the variables below, in this
order, leaving out any which are unset:

bootdev
    boot device, written at the start of the line with no prefix

bootfile
    file to load, written as 'host:<bootfile>'. With bootfile unset the
    command uses 'host:vxWorks'

ipaddr
    address of this machine, written as 'e=<ipaddr>'. Where netmask is set
    too it is appended as ':<netmask>' in hexadecimal

serverip
    address of the file server, written as 'h=<serverip>'

gatewayip
    gateway address, written as 'g=<gatewayip>'

hostname
    name of this machine, written as 'tn=<hostname>'

othbootargs
    any further text, appended to the line as it stands

The line is built in a 128-byte buffer, so keep the values short. Only the
three network fields are needed, and then only where bootdev names an
ethernet device.

addr
    address of the image. It defaults to the value of CONFIG_SYS_LOAD_ADDR

tftp
    fetch the image over TFTP before starting it, in the way the tftpboot
    command does, using the serverip and bootfile variables. The image lands
    at CONFIG_SYS_LOAD_ADDR, since the word 'tftp' takes the place of the
    address argument

On x86 the command also fills in an e820 memory map and, where a VESA mode is
active, a framebuffer information block, both at the address given by the
vx_phys_mem_base environment variable. It takes the bootline address from
there as well, so bootaddr is not needed on x86.

The command never returns when it succeeds, since it jumps to the image. The
caches are turned off first, and on arm64 with PSCI the secondary CPUs are
started.

Example
-------

Sandbox has no VxWorks image to boot, so what can be seen of the command is
the work it does beforehand. With bootaddr unset it stops at once::

    => bootvx
    ## Ethernet MAC address not copied to NV RAM
    ## VxWorks bootline address not specified
    => echo $?
    1

The first line appears on every board, since no board defines
CONFIG_SYS_VXWORKS_MAC_PTR, which is what would name the address to copy the
MAC address to.

Setting bootaddr lets the command get as far as reporting the bootline::

    => setenv bootaddr 2000
    => bootvx 3000
    ## Ethernet MAC address not copied to NV RAM
    ## VxWorks boot device not specified
    ## Using bootline (@ 0x2000): host:vxWorks e=192.0.2.1
    ## No elf image at address 0x00003000
    ## Not an ELF image, assuming binary
    ## Starting vxWorks at 0x00003000 ...

The bootline names no boot device, since bootdev is unset, and no file,
since bootfile is unset either, so it falls back to 'host:vxWorks'. Sandbox
cannot run what follows the last line: there is no image at 0x3000 and the
jump leaves U-Boot for good.

The TFTP form needs a server to talk to, so without serverip it fails before
touching the network::

    => bootvx tftp
    *** ERROR: `serverip' not set
    => echo $?
    1

Return value
------------

The return value $? is 1 (false) where the bootline address is not known,
where a TFTP transfer fails, or where the image returns to U-Boot, which a
VxWorks image is not expected to do. There is no success value to report,
since the command does not return when the image starts.

Configuration
-------------

The command is only available if CONFIG_CMD_ELF_BOOTVX=y, which needs
CONFIG_CMD_ELF=y. The tftp argument needs CONFIG_CMD_NET=y and is not
available with CONFIG_NET_LWIP=y.

See also
--------

* :doc:`bootelf<bootelf>` for starting an ELF image which is not VxWorks,
  which shares this command's file and its ELF loader
* :doc:`bootm<bootm>` for booting a uImage or FIT, which carries the load
  address and entry point in the image rather than on the command line
* :doc:`go<go>` for jumping to an address without loading anything first
* *tftpboot* for fetching the image separately from starting it
