.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: sspi (command)

sspi command
============

Synopsis
--------

::

    sspi [<bus>:]<cs>[.<mode>][@<freq>] [<bit_len>] [<dout>]

Description
-----------

The sspi command sends a string of bits to a device on a SPI bus and shows
what comes back. SPI transfers in both directions at once, so every bit sent
produces a bit received; the command prints the whole reply, including the
bytes which come back while the command itself is still being sent.

It drives the bus directly rather than through a driver for the device on the
other end, which makes it useful for trying a chip out before its driver
exists, or for checking that the wiring works at all.

bus
    number of the SPI bus, defaulting to CONFIG_DEFAULT_SPI_BUS

cs
    chip select on that bus, identifying the device to talk to

mode
    SPI mode to use, defaulting to CONFIG_DEFAULT_SPI_MODE

freq
    bus frequency in Hz, defaulting to 1000000 until another is given

bit_len
    number of bits to transfer, in decimal, at most 256

dout
    bits to send, as a string of hexadecimal digits, at most 64 of them

Each of bit_len and dout may be left out, in which case the value from the
previous sspi command is used again. Pressing Enter on an empty command line
repeats the transfer with the same values.

The reply is printed as (bit_len + 7) / 8 bytes of uppercase hexadecimal, with
no spaces and no leading 0x.

Example
-------

Sandbox has an emulated flash chip on bus 0, at chip selects 0 and 1. This
reads its JEDEC identifier, which is command 0x9f followed by three bytes
clocked out to make room for the answer::

    => sspi 0:0 32 9f000000
    SF: Detected m25p16 with page size 256 Bytes, erase size 64 KiB, total 2 MiB
    FF202015

The first byte of the reply is the FF sent back while the command byte was
going out; 20 20 15 is the identifier itself. The message above it comes from
the emulation as it starts up and is not part of the reply.

This reads the status register, command 0x05, which reports the chip as idle::

    => sspi 0:0 16 0500
    FF00

Leaving the data out sends the same bytes again, and leaving the length out as
well repeats the whole transfer::

    => sspi 0:0 16
    FF20
    => sspi 0:0
    FF20

A bus which does not exist is reported by the SPI uclass::

    => sspi 5 8 00
    spi_find_chip_select() sandbox_spi spi@0: Invalid cs 5 (err=-22)
     _spi_get_bus_and_cs() sandbox_spi spi@0: Invalid chip select 0:5 (err=-22)

The command refuses a transfer larger than it can hold::

    => sspi 0:0 300 9f
    Invalid bitlen 300

and a data string which is not hexadecimal::

    => sspi 0:0 8 gg
    Hex conversion error on g

Return value
------------

The return value $? is 0 (true) if the transfer takes place, and 1 (false) if
the bus or chip select does not exist, if the bit length or the data string is
invalid, or if the transfer itself fails.

Configuration
-------------

The command is only available if CONFIG_CMD_SPI=y. CONFIG_DEFAULT_SPI_BUS and
CONFIG_DEFAULT_SPI_MODE give the values used when the command line does not
name a bus or a mode.

See also
--------

* :doc:`sf<sf>` for reading and writing a SPI flash chip through its driver,
  rather than sending it commands by hand
* :doc:`i3c<i3c>` for talking to devices on an I3C bus
* *i2c* for talking to devices on an I2C bus
