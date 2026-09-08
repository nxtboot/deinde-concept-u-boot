.. SPDX-License-Identifier: GPL-2.0+
.. Copyright 2026 Simon Glass <sjg@chromium.org>

Debug devices
=============

Getting a message out of U-Boot while debugging normally means using the
console, but the console is not always available. It may not be set up yet,
the board may have no usable UART, or the console itself may be the thing being
investigated. A debug device is a way of getting output which does not rely on
the console: code calls it explicitly, so it stays out of the way of the normal
console handling and works even when the console does not.

The debug uclass
----------------

``UCLASS_DEBUG`` collects devices which can show debug output. A device
provides a single method, to write a string; there is no formatting, no input
and no buffering, since anything more would need memory allocation or a working
console to report its own failure to.

The easiest way to send a message is::

    #include <debug_dev.h>

    debug_puts("got here\n");

which finds the first debug device and writes to it. ``debug_puts()`` is
deliberately quiet about failure, since there is generally nowhere useful to
report it to. Where the device matters, ``debug_dev_first()`` and
``debug_dev_puts()`` give direct access.

Enable the uclass with::

    CONFIG_DEBUG_DEV=y

Sandbox provides a debug device which records what is written to it, used by
the tests in ``test/dm/debug.c``. Run them with::

    ./u-boot -T -c "ut dm debug"

Announcing a device
-------------------

When debug output does not appear there are two possibilities: the code under
investigation is not reaching its ``debug_puts()`` call, or the device itself
is not working. The second is easy to rule out if the device says something on
its own account::

    CONFIG_DEBUG_DEV_ANNOUNCE=y

With this, each debug device is probed at the end of init, on
``EVT_LAST_STAGE_INIT``, and writes a short message naming itself. It is done
then, rather than as soon as the device is bound, because a debug device may
sit on a bus which is not usable any earlier.

The EM100Pro debug device
-------------------------

Boards which boot from a flash chip emulated by a Dediprog EM100Pro have a way
out even when they have no usable UART: the emulator can carry the output over
the SPI bus it is already connected to, which needs no extra wiring at all.

The emulator watches the bus for a command which is not a flash access, 0x11 by
default, and treats what follows as a message for the host. Each message is a
reserved byte, a command telling the emulator to place what follows in its
uFIFO, a four-byte signature, a type and a length, then the data. The wire
format is shared with the debug UART below through ``include/em100.h``, and
matches the header which coreboot sends and which the em100 tool decodes.

Enable the device with::

    CONFIG_DEBUG_EM100=y

and add a node under the SPI bus, alongside the flash node the emulator
provides::

    &spi {
        em100-debug@0 {
            compatible = "dediprog,em100-debug";
            reg = <0>;
        };
    };

The command can be changed with the ``dediprog,command`` property, in case 0x11
is a real command on some flash chips. It must match what the host is looking
for, set with the em100 tool's ``--terminal-command`` option.

On the host, show the output with::

    em100 -c W25Q64DW -d u-boot.rom -p LOW -r -T

The device uses ``spi_mem_exec_op()`` rather than a plain transfer, since the
Intel ICH controller does not support the xfer() method. It builds a transaction
from an opcode, an address and data. The command becomes the opcode. Ordinary
controllers are unaffected, as spi-mem falls back to ``spi_xfer()`` for those.

Before driver model
-------------------

A debug device is only as useful as the point at which it can run, and as a
driver-model device it cannot start until driver model does. On x86 with an FSP
that is after memory init, which puts the earliest and most awkward problems
out of reach.

For those, the EM100Pro is also available as a debug UART, driving the ICH
software sequencer directly::

    CONFIG_DEBUG_UART=y
    CONFIG_DEBUG_UART_EM100=y
    CONFIG_DEBUG_UART_BASE=0xfed01000

This works from the first instruction of ``board_init_f()``, before the FSP has
run and before driver model, so ``printch()`` and ``printascii()`` are usable
throughout early init and ``debug_uart_init()`` in ``start.S`` can announce
itself with ``CONFIG_DEBUG_UART_ANNOUNCE``. It uses no writable global data,
since before relocation U-Boot runs from ROM, and needs nothing set up: on
Baytrail the boot ROM has already programmed the SPI base address register, the
opcode menu is unlocked and an addressless command is not subject to the BIOS
write-protect. If the menu is locked, the command must already be on it.

The debug UART and the driver-model device are two front ends over one wire
format: the debug UART before driver model, the device once it is up.

.. warning::

   Both send command 0x11 on the SPI bus. That is harmless with an emulator
   attached, which is how such a board is normally used, but 0x11 is a real
   command on some flash chips, so do not enable this when booting from a chip
   soldered to the board.

Programming interface
---------------------

.. kernel-doc:: include/debug_dev.h
