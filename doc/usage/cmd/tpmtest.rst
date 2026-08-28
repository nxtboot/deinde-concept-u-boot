.. SPDX-License-Identifier: GPL-2.0+:

.. index::
   single: tpmtest (command)

tpmtest command
===============

Synopsis
--------

::

    tpmtest <subtest>

Description
-----------

The tpmtest command runs one of a suite of checks against a TPMv1.x device,
to confirm that the chip and its driver behave as the rest of U-Boot expects.
Each subtest drives the TPM through a sequence of ordinals and reports what it
finds, so this is a bring-up tool rather than something to run on every boot.

The subtest acts on the currently selected TPM, which is chosen with
'tpm device'. There is no way to select one from this command, and the default
is the first TPM in the system, so a board with more than one needs the
selection made first.

Two lines come out before any subtest starts: the arguments the command was
given, and a rule. They are debugging output which the command has always
printed.

early_extend
    Extend a PCR before the TPM has been started up.

early_nvram, early_nvram2
    Read and write a non-volatile space before startup, with and without the
    space having been defined.

enable, fast_enable
    Turn the TPM off and on again through the physical-presence route,
    checking the disable and deactivated flags at each step. fast_enable does
    the same thing by the shorter path, which a real chip completes more
    quickly.

global_lock
    Check that the global lock stops a further write to a locked space.

lock
    Lock a single space and check that it stays locked.

readonly
    Check that a space defined read-only refuses a write.

redefine_unowned
    Redefine a space on a TPM which has no owner.

space_perm
    Check the permissions a space is created with.

startup
    Run the startup sequence and a full self test.

timing
    Time each of the common operations and report how long it took, to show up
    a chip which is slower than the boot can afford.

write_limit
    Write a space repeatedly to reach the limit a TPM imposes between owner
    clears.

timer
    Show the value of get_timer(0). This one is a check of the board rather
    than of the TPM, and the built-in help does not list it.

subtest
    name of the check to run, exactly as spelled above

Example
-------

Sandbox emulates a TPMv1.x device as device 1, so it has to be selected
first. Several of the subtests run against the emulation::

    => tpm device 1
    => tpmtest startup
    argc = 2, argv =  tpmtest startup
    ------
    Testing startup ...
    Get flags index 0x108
    	executing SelfTestFull
    Get flags index 0x108
    	done
    => tpmtest early_extend
    argc = 2, argv =  tpmtest early_extend
    ------
    Testing earlyextend ...done

The 'Get flags index' lines come from the emulation, not from the subtest.

A subtest which fails says where it stopped and what the TPM returned::

    => tpmtest early_nvram
    argc = 2, argv =  tpmtest early_nvram
    ------
    Testing earlynvram ...Invalid nv index 0xda70
    TEST FAILED: line 110: tpm_nv_read_value(dev, INDEX0, (uint8_t *)&x, sizeof(x)): 0xffffffea

The emulation covers only the non-volatile spaces a Chromium OS device uses,
so the subtests which reach for another index fail on sandbox. That is a limit
of the emulation rather than something the suite has found.

A name which is not a subtest produces the list of those which are::

    => tpmtest bogus
    argc = 2, argv =  tpmtest bogus
    ------
    tpmtest - TPM tests

    Usage:
    tpmtest
    	early_extend
    	early_nvram
    ...

Configuration
-------------

The tpmtest command is available if CONFIG_CMD_TPM_TEST=y, which needs
CONFIG_CMD_TPM=y and CONFIG_TPM_V1=y.

Return value
------------

The return value $? is 0 (true) if the subtest passes and 1 (false) if it
fails. A missing or unrecognised subtest is reported as a usage error, which
is also 1 (false).

See also
--------

* :doc:`tpm<tpm>` for selecting the TPM to test and driving it by hand
