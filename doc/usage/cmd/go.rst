.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: go (command)

go command
==========

Synopsis
--------

::

    go <addr> [<arg> ...]

Description
-----------

The go command calls the code at a given address, as an ordinary function
call. It is the way to run a standalone application: a small program built
against U-Boot's own API which does its work and then returns to the command
line, unlike an operating system, which never comes back.

Nothing is loaded, relocated, decompressed or checked. The address must
already hold the code, put there by :doc:`load<load>`, tftpboot or whatever
suits, and it must be the entry point rather than the start of an image
header. The command does not look at the memory before jumping to it, so an
address which holds something else usually takes the board down.

The application is called with the arguments given, and with the address
itself as *argv[0]*, in the same form as it was typed. It therefore sees an
*argc* of at least 1 even when no arguments follow the address.

The value the application returns is printed, so it can carry information
which does not fit into the return value of the command.

The call itself is a per-architecture function, do_go_exec(), which some
boards use to do more than a plain call: arm sets bit 0 of the address so that
the processor stays in Thumb state, x86 passes the global data pointer as
*argv[-1]*, and the Xilinx ZynqMP and Versal boards start the code on an R5
core rather than in place.

addr
    entry point of the application, hexadecimal

arg
    arguments to pass to the application, after the address

Example
-------

This is the sandbox test for the command, which calls a function inside U-Boot
itself since sandbox has no separate application to start::

    => go 5b5262f81e81 hello world
    ## Starting application at 0x5B5262F81E81 ...
    app: 5b5262f81e81 hello world
    ## Application terminated, rc = 0x0

The middle line is the application's own output, showing that it received the
address as its first argument. An application which reports a problem does so
through its return value::

    => go 5b5262f81e81
    ## Starting application at 0x5B5262F81E81 ...
    app: 5b5262f81e81
    ## Application terminated, rc = 0x1234
    => echo $?
    1

Configuration
-------------

The go command is available if CONFIG_CMD_GO=y, which is the default.

Return value
------------

The return value $? is 0 (true) if the application returns 0. It is 1 (false)
for any other value the application returns, and also if no address is given,
in which case the usage message is printed and nothing is called.

An application which does not return never gets as far as setting a return
value.

See also
--------

* :doc:`bootelf<bootelf>` for starting an ELF file, which finds the entry
  point in the file rather than being told it
* :doc:`iminfo<iminfo>` for the entry point recorded in an image header
* :doc:`load<load>` for reading an application into memory first
* :doc:`bootm<bootm>` for booting an operating system, which does not return
