.. SPDX-License-Identifier: GPL-2.0+

Booting Ubuntu live ISOs via U-Boot
===================================

U-Boot can replace GRUB as the bootloader on an Ubuntu live ISO. The
stock ISO has an appended EFI system partition (ESP) containing shim,
GRUB and their configuration. ``scripts/ubuntu-iso-to-uboot.py`` rewrites
that ESP with a U-Boot EFI application, a :doc:`Boot Loader Specification
Type #1 </usage/bls>` entry and the casper kernel and initrd, leaving the
rest of the ISO untouched so casper still finds its squashfs by disc
label at runtime.

The flow only touches the appended partition. BIOS El Torito, the
grub2 MBR, the GPT layout and the ISO 9660 tree are preserved verbatim
by xorriso's ``-boot_image any replay``

Host prerequisites
------------------

::

    sudo apt install xorriso mtools dosfstools qemu-system-x86 ovmf

Building the U-Boot EFI application
-----------------------------------

The ``efi-x86_app64`` target enables ``CONFIG_BOOTMETH_BLS=y``,
``CONFIG_FS_ISOFS=y`` and ``CONFIG_JOLIET=y`` by default, so no
Kconfig tweaks are required. Build with::

    make O=/tmp/b/efi-x86_app64 efi-x86_app64_defconfig
    make O=/tmp/b/efi-x86_app64 -j$(nproc)

The output is ``/tmp/b/efi-x86_app64/u-boot-app.efi``, a PE32+ x86_64 EFI
application.

If ``rustc`` is not installed, also disable the rust example build
before ``make``::

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
2. Extracts ``/casper/vmlinuz`` and ``/casper/initrd`` via ``xorriso -osirrox``
3. Builds a fresh FAT32 ESP (auto-sized to fit) containing
   ``/EFI/BOOT/BOOTX64.EFI`` (the U-Boot app), ``/casper/vmlinuz``,
   ``/casper/initrd`` and ``/loader/entry.conf``
4. Writes a new ISO with ``xorriso -indev ... -outdev ... -boot_image
   any replay -append_partition 2 ...``, replacing the original ESP
   while preserving all other boot metadata.

Relevant options:

* ``-u PATH`` -- the U-Boot EFI application (required).
* ``-o PATH`` -- the output ISO (required).
* ``-a ARGS`` -- override the kernel command line written to
  ``loader/entry.conf``. The default is
  ``console=ttyS0,115200 console=tty0 --- quiet``, which logs the kernel output
  serial and video. Duplicate the ``console=`` arguments after ``---`` as well
  if you want casper and the running system logged to serial too.
* ``-k PATH`` / ``-i PATH`` -- override the kernel and initrd paths
  inside the ISO if a distribution uses something other than
  ``casper/vmlinuz`` and ``casper/initrd``.
* ``-s MiB`` -- force an ESP size; the default auto-sizes to fit the
  kernel, initrd and U-Boot app with 16 MiB of headroom.
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

    BdsDxe: loading Boot0001 "UEFI Misc Device" from PciRoot(0x0)/Pci(0x3,0x0)
    BdsDxe: starting Boot0001 "UEFI Misc Device" from PciRoot(0x0)/Pci(0x3,0x0)
    Laceboot EFI App (using allocated RAM address 6beef000) starting


    U-Boot Concept 2026.02

    CPU: x86_64, vendor <invalid cpu vendor>, device 0h
    Model: EFI x86 Application
    DRAM:  256 MiB
    Core:  22 devices, 13 uclasses, devicetree: separate
    EFI:   disks 2, partitions 3
    Loading Environment from FAT... Unable to use efi 0:0...
    Video: 1280x800x32 @ 0
    Hit any key to stop autoboot:  2
    Hit any key to stop autoboot: 1
    Hit any key to stop autoboot: 0
    Scanning for bootflows in all bootdevs
    Seq  Method       State   Uclass    Part  Ent  E  Name                      Filename
    ---  -----------  ------  --------  ----  ---  -  ------------------------  ----------------
    Hunting with: fs
    Scanning bootdev 'efi_media_0.bootdev':
      0  bls          ready   pci          2    0     efi_media_0.bootdev.part_ /loader/entry.conf
    ** Booting bootflow 'efi_media_0.bootdev.part_2' with bls
    Retrieving file: /casper/vmlinuz
    Retrieving file: /casper/initrd
    Valid Boot Flag
    Magic signature found
    Linux kernel version 6.8.0-41-generic (buildd@lcy02-amd64-100) #41-Ubuntu SMP PREEMPT_DYNAMIC Fri Aug  2 20:41:06 UTC 2024
    Building boot_params at 90000
    Loading bzImage at address 100000 (14926336 bytes)
    Initial RAM disk at linear address 8000000, size 47a003e (75104318 bytes)
    Kernel command line: "console=ttyS0,115200 console=tty0 --- console=ttyS0,115200 quiet"

    Starting kernel ...

    ...


Why the kernel and initrd live on the ESP
-----------------------------------------

BLS paths are resolved against the partition holding ``loader/entry.conf``

OVMF only exposes the EFI system partition as an ``EFI_BLOCK_IO`` protocol
handle on CD media, so U-Boot's ``efi_media`` bootdev cannot see the ISO 9660
partition directly and would fail to resolve ``/casper/vmlinuz`` if the entry
were placed there. Copying the kernel and initrd onto the ESP sidesteps this;
the ISO 9660 side remains bootable in its own right, and casper finds its
squashfs on the original media at runtime.

See also
--------

* :doc:`/usage/bls` — the U-Boot Boot Loader Specification bootmeth.
* `Boot Loader Specification
  <https://uapi-group.org/specifications/specs/boot_loader_specification/>`_.
