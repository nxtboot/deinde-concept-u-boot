.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: iotrace (command)

iotrace command
===============

Synopsis
--------

::

    iotrace buffer [<address> <size>]
    iotrace limit [<address> <size>]
    iotrace resume
    iotrace pause
    iotrace stats
    iotrace dump

Description
-----------

The iotrace command controls the I/O tracer, which records every I/O access
U-Boot makes into a buffer in memory. Each record holds a timestamp, the
address, the value and whether the access is a read or a write, so the
sequence a driver produces can be examined afterwards, or compared between
runs through the checksum which is kept alongside.

Tracing works by redirecting the readb(), readw(), readl(), writeb(),
writew() and writel() accessors, so only accesses made with those are seen. It
cannot start before relocation, since the buffer is set by this command, and
accesses made with a plain pointer or with the 64-bit accessors are not
recorded.

Only the first letter of the sub-command is examined, so each may be
abbreviated to it: 'iotrace b' sets the buffer.

iotrace buffer [<address> <size>]
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Points the tracer at a buffer and starts a fresh trace, resetting the write
offset, the needed size and the checksum. With no arguments the buffer is
dropped, which stops anything being recorded.

Nothing is recorded until tracing is resumed, so a buffer can be set up before
the operation of interest begins.

address
    start address of the buffer, hexadecimal

size
    size of the buffer in bytes, hexadecimal. Recording stops when the buffer
    is full, with a warning suggesting 'iotrace stats' to find the size the
    trace actually needs.

iotrace limit [<address> <size>]
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Limits tracing to a region of the address space, so that accesses outside it
are ignored. This keeps the buffer for the device under investigation rather
than filling it with traffic from every other driver. With no arguments the
limit is removed and the whole address space is traced again.

address
    start address of the region to trace, hexadecimal

size
    size of the region in bytes, hexadecimal

The addresses compared against the region are the ones 'iotrace dump' shows,
which on sandbox are addresses in emulated memory rather than pointers into
the host address space.

iotrace resume
~~~~~~~~~~~~~~

Starts recording, or continues after a pause. Tracing is off when U-Boot
starts.

iotrace pause
~~~~~~~~~~~~~

Stops recording, leaving the buffer and its contents alone, so that a trace
can be collected around one operation and read back afterwards.

iotrace stats
~~~~~~~~~~~~~

Shows the state of the tracer:

iotrace is enabled/disabled
    whether accesses are being recorded

Start, Actual Size
    the buffer set by 'iotrace buffer'

Needed Size
    the space every access since the buffer was set would have needed. Where
    this exceeds the actual size, records have been dropped and this is the
    size to use instead.

Region, Size
    the region set by 'iotrace limit', or zero if tracing is not limited

Offset, Output
    the offset the next record is written at, and the address it lands on

Count
    the number of records in the buffer

CRC32
    the checksum of every record, including those dropped for want of space.
    Two runs of the same operation produce the same checksum where the driver
    behaves the same way, although the timestamps in each record mean this
    only holds if the timing does not vary.

iotrace dump
~~~~~~~~~~~~

Lists the records in the buffer, one per line: the timestamp in
microseconds, the value, then the address. A read is shown as
``value <-- address`` and a write as ``value --> address``. Nothing is printed
when no buffer is set or no records have been collected.

Example
-------

This traces the accesses one command makes on sandbox, where the emulated
power-management controller is reached with readl()::

    => iotrace buffer 1000000 10000
    => iotrace resume
    => pmc info
    Device: pci@1e,0
    ACPI base 0, pmc_bar0 000000001688d690, pmc_bar2 0000000000000000, gpe_cfg 000000001688e6e0
    pm1_sts: 0000 pm1_en: 0002 pm1_cnt: 00000004
    gpe0_sts[0]: 00000020 gpe0_en[0]: 00000030
    gpe0_sts[1]: 00000024 gpe0_en[1]: 00000034
    gpe0_sts[2]: 00000028 gpe0_en[2]: 00000038
    gpe0_sts[3]: 0000002c gpe0_en[3]: 0000003c
    prsts: 00000000
    tco_sts:   0064 0066
    gen_pmcon1: 00000000 gen_pmcon2: 00000000 gen_pmcon3: 00000000
    => iotrace pause
    => iotrace stats
    iotrace is disabled
    Start:  01000000
    Actual Size:   00010000
    Needed Size:   00000080
    Region: 00000000
    Size:   00000000
    Offset: 00000080
    Output: 01000080
    Count:  00000004
    CRC32:  dd6f774a
    => iotrace dump
    Timestamp  Value          Address
    1902419319372: 0x00000000 <-- 0x0688d690
    1902419319374: 0x00000000 <-- 0x0688d6b0
    1902419319374: 0x00000000 <-- 0x0688d6b4
    1902419319374: 0x00000000 <-- 0x0688d6b8

The four accesses all fall in one small region, so a limit covering it keeps
them and discards everything else::

    => iotrace limit 6000000 1000000
    => iotrace buffer 1000000 10000
    => iotrace resume
    => pmc info
    ...
    => iotrace pause
    => iotrace stats
    iotrace is disabled
    Start:  01000000
    Actual Size:   00010000
    Needed Size:   00000080
    Region: 06000000
    Size:   01000000
    Offset: 00000080
    Output: 01000080
    Count:  00000004
    CRC32:  20680c4c

A buffer too small for the trace reports the size it wants::

    => iotrace buffer 1000000 20
    => iotrace resume
    => pmc info
    WARNING: iotrace buffer exhausted, please check needed length using "iotrace stats"
    Device: pci@1e,0
    ...
    => iotrace stats
    iotrace is enabled
    Start:  01000000
    Actual Size:   00000020
    Needed Size:   00000080
    ...

Configuration
-------------

The iotrace command is available if CONFIG_CMD_IOTRACE=y, which requires
CONFIG_IO_TRACE=y. Tracing every I/O access slows U-Boot down, so both are
intended for debugging rather than for a production build.

Return value
------------

The return value $? is 0 (true) if the sub-command runs. It is 1 (false) if no
sub-command is given, if the first letter matches none of them, or if 'iotrace
buffer' or 'iotrace limit' is given one argument rather than none or two.

See also
--------

* :doc:`trace<trace>` for tracing function calls rather than I/O accesses
* :doc:`iod<iod>`, :doc:`iow<iow>` for reading and writing I/O space by hand
* :doc:`md<md>` for displaying the trace buffer as raw memory
* :doc:`pmc<pmc>` for the command which produces the accesses traced above
* *pcap* for capturing network packets to a buffer in the same way
