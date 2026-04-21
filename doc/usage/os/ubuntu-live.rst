.. SPDX-License-Identifier: GPL-2.0+

Booting Ubuntu live ISOs via U-Boot
===================================

U-Boot can replace GRUB as the bootloader on an Ubuntu live ISO.
``scripts/ubuntu-iso-to-uboot.py`` rewrites the ISO so the appended EFI
system partition holds a U-Boot EFI application and the ISO 9660 tree
carries a :doc:`Boot Loader Specification Type #1 </usage/bls>` entry
pointing at the casper kernel and initrd that Ubuntu already places on
the disc.

All other boot records (BIOS El Torito, grub2 MBR, GPT layout) are
preserved verbatim by xorriso's ``-boot_image any replay``, and casper
still finds its squashfs by disc label at runtime.

Host prerequisites
------------------

::

    sudo apt install xorriso mtools dosfstools qemu-system-x86 ovmf

Building the U-Boot EFI application
-----------------------------------

The ``efi-x86_app64`` target enables ``CONFIG_BOOTMETH_BLS=y``,
``CONFIG_FS_ISOFS=y`` and ``CONFIG_JOLIET=y`` by default, so no Kconfig
tweaks are required. Build with::

    make O=/tmp/b/efi-x86_app64 efi-x86_app64_defconfig
    make O=/tmp/b/efi-x86_app64 -j$(nproc)

The output is ``/tmp/b/efi-x86_app64/u-boot-app.efi``, a PE32+ x86_64
EFI application.

If ``rustc`` is not installed, also disable the rust example build
before the main ``make``::

    scripts/config --file /tmp/b/efi-x86_app64/.config \\
        -d RUST_EXAMPLES -d EXAMPLES
    make O=/tmp/b/efi-x86_app64 olddefconfig

Rewriting an Ubuntu ISO
-----------------------

Fetch the ISO (desktop and server images both work) and run the
helper::

    curl -LO https://releases.ubuntu.com/24.04.1/ubuntu-24.04.1-desktop-amd64.iso

    scripts/ubuntu-iso-to-uboot.py ubuntu-24.04.1-desktop-amd64.iso \\
        -u /tmp/b/efi-x86_app64/u-boot-app.efi \\
        -o ubuntu-uboot.iso

The script:

1. Reads the input ISO's boot record with ``xorriso -report_el_torito``
   to pick up the volume label and the EFI system partition GUID.
2. Builds a small FAT ESP (4 MiB by default) containing just
   ``/EFI/BOOT/BOOTX64.EFI`` -- the U-Boot EFI application.
3. Writes a new ISO with
   ``xorriso -indev ... -outdev ... -boot_image any replay
   -append_partition 2 ... -map entry.conf /loader/entry.conf``,
   which replaces the original ESP and adds ``/loader/entry.conf`` to
   the ISO 9660 tree. The kernel and initrd stay in ``/casper/`` on the
   ISO 9660 tree; U-Boot reads them directly via its isofs driver.
4. Strips the shim, GRUB and MOK manager binaries
   (``/EFI/boot/{bootx64,grubx64,mmx64}.efi``) from the ISO 9660 tree.
   The UEFI firmware loads ``BOOTX64.EFI`` from the appended ESP, so
   the ISO 9660 copies are unused dead weight. The BIOS El Torito
   image under ``/boot/grub/`` is left in place, so legacy-BIOS boot
   still chains into GRUB as before.

Relevant options:

* ``-u PATH`` -- the U-Boot EFI application (required).
* ``-o PATH`` -- the output ISO (required).
* ``-a ARGS`` -- override the kernel command line written to
  ``loader/entry.conf``. The default is
  ``console=ttyS0,115200 console=tty0 --- quiet``, which logs the
  kernel to serial and video. Duplicate the ``console=`` arguments
  after ``---`` as well if you want casper and the running system
  logged to serial too.
* ``-k PATH`` / ``-i PATH`` -- override the kernel and initrd paths
  inside the ISO if a distribution uses something other than
  ``casper/vmlinuz`` and ``casper/initrd``.
* ``-s MiB`` -- force an ESP size; the default is 4 MiB, enough for the
  U-Boot binary.
* ``-t TITLE`` -- override the BLS entry title.

Testing under QEMU + OVMF
-------------------------

::

    cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS.fd
    qemu-system-x86_64 -machine q35 -m 4096 -smp 2 -enable-kvm \\
        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \\
        -drive if=pflash,format=raw,file=/tmp/OVMF_VARS.fd \\
        -drive if=virtio,file=ubuntu-uboot.iso,format=raw,readonly=on

The expected boot trace on the serial console is roughly::

    U-Boot Concept 2026.02
    ...
    Scanning bootdev 'efi_media_0.bootdev':
      0  bls    ready   pci  1  0  efi_media_0.bootdev.part_ /loader/entry.conf
    ** Booting bootflow 'efi_media_0.bootdev.part_1' with bls
    Retrieving file: /casper/vmlinuz
    Retrieving file: /casper/initrd
    Linux kernel version 6.8.0-41-generic ... Ubuntu
    Starting kernel ...

The bootflow appears on ``part_1`` -- the ISO 9660 partition -- because
``entry.conf`` lives in the ISO 9660 tree and U-Boot reads kernel and
initrd from the same partition via isofs.

See also
--------

* :doc:`/usage/bls` -- the U-Boot Boot Loader Specification bootmeth.
* `Boot Loader Specification
  <https://uapi-group.org/specifications/specs/boot_loader_specification/>`_.
