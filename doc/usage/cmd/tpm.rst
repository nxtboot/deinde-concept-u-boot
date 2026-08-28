.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: tpm (command)

tpm command
===========

Synopsis
--------

::

    tpm device [<num>]
    tpm info
    tpm init
    tpm autostart
    tpm startup <mode>
    tpm self_test_full
    tpm continue_self_test
    tpm force_clear
    tpm physical_enable
    tpm physical_disable
    tpm physical_set_deactivated <0|1>
    tpm tsc_physical_presence <flags>
    tpm get_capability <cap_area> <sub_cap> <addr> <count>
    tpm read_pubek <addr> <count>
    tpm extend <index> <digest>
    tpm pcr_read <index> <addr> <count>
    tpm nv_define_space <index> <permission> <size>
    tpm nv_read_value <index> <addr> <count>
    tpm nv_write_value <index> <byte_string>
    tpm nv_define <type_string> <index> <permission>
    tpm nv_read <type_string> <index> <var>...
    tpm nv_write <type_string> <index> <value>...
    tpm raw_transfer <byte_string>

Description
-----------

The tpm command talks to a Trusted Platform Module: a chip which holds
measurements of the boot in its Platform Configuration Registers (PCRs) and
keeps a small amount of non-volatile storage which the rest of the system
cannot rewrite at will.

The sub-commands act on the currently selected TPM, chosen with 'tpm device'
and remembered until it is changed. When nothing has been selected the first
TPM in the system is used.

Which sub-commands exist depends on the version of the selected device, not on
the name typed. This page describes a TPMv1.x device; a TPMv2.x one has a
different set, described by *tpm2*. The two commands share a handler, so 'tpm'
on a board whose first TPM is a TPMv2.x device offers the TPMv2.x
sub-commands, and 'tpm2' on a TPMv1.x device offers these ones. Only the help
text differs.

Most of the sub-commands are a thin wrapper around a single TPM ordinal, so
the TPM specification is the reference for what each one means.

Every numeric argument, the memory addresses included, is read in the base its
prefix gives: 0x for hexadecimal, a leading 0 for octal, otherwise decimal.
This differs from most U-Boot commands, where an address is hexadecimal
whether or not it is prefixed, so an address here needs its 0x.

tpm device
~~~~~~~~~~

Show the TPMs in the system, each by its number and description, or select the
one which later sub-commands act on.

tpm info
~~~~~~~~

Show the description of the selected TPM.

tpm init
~~~~~~~~

Open the TPM and leave it waiting for a startup, without issuing one.

tpm autostart
~~~~~~~~~~~~~

Do the whole opening sequence: init, a Startup(clear) and a full self test.
This is the usual way to bring a TPM up, since the three are almost always
wanted together.

tpm startup
~~~~~~~~~~~

Issue TPM_Startup. The mode is TPM_ST_CLEAR to start with fresh PCRs,
TPM_ST_STATE to restore the saved state, or TPM_ST_DEACTIVATED to start
deactivated. The name is matched without regard to case.

tpm self_test_full, tpm continue_self_test
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Run the full self test, or tell the TPM to finish a test it has already
started.

tpm force_clear
~~~~~~~~~~~~~~~

Clear the owner and everything belonging to it.

tpm physical_enable, tpm physical_disable
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Clear or set the permanent disable flag, using physical presence as the
authorisation.

tpm physical_set_deactivated
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Set the deactivated flag to 0 or 1.

tpm tsc_physical_presence
~~~~~~~~~~~~~~~~~~~~~~~~~

Set the physical-presence flags of the device.

tpm get_capability
~~~~~~~~~~~~~~~~~~

Read a capability of the TPM into memory, then show it as hex bytes.

tpm read_pubek
~~~~~~~~~~~~~~

Read the public endorsement key into memory, then show it as hex bytes.

tpm extend
~~~~~~~~~~

Add a measurement to a PCR. The digest is 20 bytes given as 40 hex digits. The
TPM replaces the register with the hash of its old contents followed by the
digest, so the order in which measurements arrive is part of the result. The
new contents are shown.

tpm pcr_read
~~~~~~~~~~~~

Read a PCR into memory, then show it as hex bytes. The count must be at least
20, the length of a TPMv1.x PCR.

tpm nv_define_space, tpm nv_read_value, tpm nv_write_value
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Create a non-volatile space of a given size, read one into memory, or write one
from a string of hex digits. Note that the write takes the data itself, not an
address and a count.

tpm nv_define, tpm nv_read, tpm nv_write
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The same three operations, but treating the space as a sequence of packed
big-endian integers rather than as bytes. The type string has one character per
value: 'b' for a byte, 'w' for two bytes, 'd' for four. A read writes one
environment variable per value; a write takes one value per character.

tpm raw_transfer
~~~~~~~~~~~~~~~~

Send a command to the TPM as raw bytes and show the response, for anything the
sub-commands above do not cover.

num
    number of the TPM to use, decimal, counting from 0 in the order the
    devices are bound

mode
    startup mode, one of TPM_ST_CLEAR, TPM_ST_STATE and TPM_ST_DEACTIVATED

flags
    physical-presence flags

cap_area, sub_cap
    capability area and the capability within it

index
    PCR number, or the index of a non-volatile space

permission
    permission bits of a non-volatile space

size, count
    number of bytes

addr
    memory address of the buffer to read into

digest, byte_string
    data given as hex digits, two per byte, with nothing between them

type_string
    layout of a non-volatile space, a string of 'b', 'w' and 'd' characters

var
    name of an environment variable to write a value to

value
    value to write, hexadecimal with a 0x prefix, or decimal

Example
-------

Sandbox emulates a TPMv1.x device as well as a TPMv2.x one, so the version
wanted has to be selected. Bringing the TPMv1.x device up produces no output::

    => tpm device
    device 0: Sandbox TPM2.x
    device 1: sandbox TPM
    => tpm device 1
    => tpm info
    sandbox TPM
    => tpm autostart

Extending a PCR shows what the register then holds. The emulation does not
compute the hash, so it stays at zero::

    => tpm extend 0 0102030405060708090a0b0c0d0e0f1011121314
    PCR value after execution of the command:
     00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
     00 00 00 00

A non-volatile space is created before it can be used. Reading one leaves the
data in memory as well as showing it::

    => tpm nv_define_space 0x1007 0 4
    tpm: define_space index=0x1007, len=0x4, seq=0x2
    => tpm nv_write_value 0x1007 deadbeef
    tpm: nvwrite index=0x1007, len=0x4
    => tpm nv_read_value 0x1007 0x2000 4
    tpm: nvread index=0x1007, len=0x4, seq=0x2
    area content:
     de ad be ef

The lines beginning 'tpm:' come from the emulation, not from the command.

The helper form stores the same space as one four-byte integer, and reads it
back into an environment variable::

    => tpm nv_define d 0x1008 0
    tpm: define_space index=0x1008, len=0x4, seq=0x3
    => tpm nv_write d 0x1008 0x12345678
    tpm: nvwrite index=0x1008, len=0x4
    => tpm nv_read d 0x1008 myvar
    tpm: nvread index=0x1008, len=0x4, seq=0x3
    => printenv myvar
    myvar=305419896

A raw transfer sends the bytes as they are given. This one is a full self
test, which has nothing to report::

    => tpm raw_transfer 00c10000000a00000050
    tpm response:
     00 00 00 00 00 00 00 00 00 00 00 00

A device which is not there, a startup mode which is not recognised and a
non-volatile space which the device does not have are all reported::

    => tpm device 5
    Couldn't set TPM 5 (rc = 1)
    => tpm startup FOO
    Couldn't recognize mode string: FOO
    => tpm nv_read_value 0x9999 0x2000 4
    Invalid nv index 0x9999
    Error: -22

Configuration
-------------

The tpm command is available if CONFIG_CMD_TPM=y, which needs CONFIG_TPM_V1=y
or CONFIG_TPM_V2=y and a driver for the TPM itself.

The oiap, end_oiap, load_key2_oiap and get_pub_key_oiap sub-commands are
available if CONFIG_TPM_AUTH_SESSIONS=y, load_key_by_sha1 also needs
CONFIG_TPM_LOAD_KEY_BY_SHA1=y, flush needs CONFIG_TPM_FLUSH_RESOURCES=y and
list needs CONFIG_TPM_LIST_RESOURCES=y.

Return value
------------

The return value $? is 0 (true) if the operation succeeds. It is 1 (false) if
the TPM reports an error, in which case the error code is shown first, and also
if the device given to 'tpm device' does not exist.

A missing or unrecognised sub-command, and a wrong number of arguments, are
reported as a usage error, which is also 1 (false).

See also
--------

* :doc:`md<md>` for showing the memory that a read fills in
* *tpm2* for the same command driving a TPMv2.x device
* *tpmtest* for a suite of tests which exercise a TPMv1.x device
