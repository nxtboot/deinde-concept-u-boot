.. SPDX-License-Identifier: GPL-2.0+

.. index::
   single: reset (command)

reset command
=============

Synopsis
--------

::

    reset [-c] [-w] [-u]

Description
-----------

Perform reset of the CPU. By default does COLD reset, which resets CPU,
DDR and peripherals, on some boards also resets external PMIC.

-w
    Do WARM reset: reset CPU but keep peripheral/DDR/PMIC active.

-h
    Do a hot reset, if supported, which returns back to the program which
    started U-Boot.

-u
    Reset to firmware UI (EFI app only). Sets the
    EFI_OS_INDICATIONS_BOOT_TO_FW_UI bit in the OsIndications variable and
    performs a warm reset, causing the firmware to boot directly to its
    setup/configuration interface.

-edl
    Boot to Emergency DownLoad mode on supported Qualcomm platforms.

Return value
------------

The return value $? is always set to 0 (true).
