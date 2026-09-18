.. SPDX-License-Identifier: GPL-2.0+:

EFI Boot Manager Bootmeth
=========================

The EFI boot-manager bootmeth delegates boot-device selection to the UEFI boot
manager. Rather than scanning filesystems for a specific binary, it checks
whether the ``BootNext`` and ``BootOrder`` EFI variables name a usable boot
option and, if so, marks the bootflow as ready.

This is a global bootmeth: it is not tied to a particular bootdev but is
invoked once during each scan. The ``BOOTMETHF_GLOBAL`` flag is set at bind
time, and the global priority is ``BOOTDEVP_6_NET_BASE`` so that it runs just
before very slow devices, giving filesystem-based methods a chance to complete
first.

During discovery, ``efi_mgr_read_bootflow()`` first hunts the bootdevs which
do not need a slow bus scan, so that their block devices exist: the EFI
variable store loads its file from the EFI system partition when the EFI
objects are set up, and the boot manager resolves each boot option's device
path against the disks U-Boot has probed. It then initialises the EFI object
list and looks for a usable boot option: the one named by ``BootNext``, or
else the first active entry of ``BootOrder`` which can be parsed. If there is
one, the bootflow is marked ready and takes the option's label as its name,
e.g. 'Windows Boot Manager'; otherwise the method is skipped.

At boot time, ``efi_mgr_boot()`` calls ``efi_bootmgr_run()`` which walks the
``BootOrder`` list and launches the first viable EFI application. If nothing
loads, the option's device may be on a bus which the discovery hunt left out
because it is slow to scan, such as USB: the bootmeth hunts those buses and,
if that turned up anything new, tries once more. No file loading is done by
the bootmeth itself.

The compatible string "u-boot,efi-bootmgr" is used for the driver. It is
present if `CONFIG_BOOTMETH_EFI_BOOTMGR` is enabled.

See :doc:`/develop/uefi/uefi` for general UEFI implementation details and
:doc:`/usage/cmd/eficonfig` for configuring boot entries.
