.. SPDX-License-Identifier: GPL-2.0+

Booting Ubuntu live ISOs via U-Boot
===================================

U-Boot can replace GRUB as the bootloader on an Ubuntu live ISO.
``scripts/ubuntu-iso-to-uboot.py`` rewrites the ISO so the appended EFI system
partition holds a U-Boot EFI application and the ISO 9660 tree carries
a :doc:`Boot Loader Specification Type #1 </usage/bls>` entry pointing at the
casper kernel and initrd that Ubuntu already places on the disc.

All other boot records (BIOS El Torito, grub2 MBR, GPT layout) are preserved
verbatim by xorriso's ``-boot_image any replay``, and casper still finds its
squashfs by disc label at runtime.

Host prerequisites
------------------

::

    sudo apt install xorriso mtools dosfstools squashfs-tools fakeroot \\
        qemu-system-x86 ovmf

``squashfs-tools`` and ``fakeroot`` are only needed when the script modifies the
install squashfs (see :ref:`target-bls-setup`); pass ``-N`` to skip that step
if you'd rather not install them.

Building the U-Boot EFI application
-----------------------------------

The ``efi-x86_app64`` target enables ``CONFIG_BOOTMETH_BLS=y``,
``CONFIG_FS_ISOFS=y`` and ``CONFIG_JOLIET=y`` by default, so no Kconfig tweaks
are required. Build with::

    make O=/tmp/b/efi-x86_app64 efi-x86_app64_defconfig
    make O=/tmp/b/efi-x86_app64 -j$(nproc)

The output is ``/tmp/b/efi-x86_app64/u-boot-app.efi``, a PE32+ x86_64 EFI
application.

If ``rustc`` is not installed, also disable the rust example build before the
main ``make``::

    scripts/config --file /tmp/b/efi-x86_app64/.config \\
        -d RUST_EXAMPLES -d EXAMPLES
    make O=/tmp/b/efi-x86_app64 olddefconfig

Rewriting an Ubuntu ISO
-----------------------

Fetch the ISO (desktop and server images both work) and run the helper::

    curl -LO https://releases.ubuntu.com/24.04.1/ubuntu-24.04.1-desktop-amd64.iso

    scripts/ubuntu-iso-to-uboot.py ubuntu-24.04.1-desktop-amd64.iso \\
        -u /tmp/b/efi-x86_app64/u-boot-app.efi \\
        -o ubuntu-uboot.iso

The script:

1. Reads the input ISO's boot record with ``xorriso -report_el_torito`` to pick
   up the volume label and the EFI system partition GUID, and parses
   ``/boot/grub/grub.cfg`` to inherit the kernel command line from the source
   ISO's own default entry (``--- quiet splash`` on Ubuntu 24.04.1).
2. Builds a small FAT ESP (4 MiB by default) containing just
   ``/EFI/BOOT/BOOTX64.EFI`` -- the U-Boot EFI application.
3. Writes a new ISO with ``xorriso -indev ... -outdev ... -boot_image any replay
   -append_partition 2 ... -map entry.conf /loader/entry.conf``, which replaces
   the original ESP and adds ``/loader/entry.conf`` to the ISO 9660 tree. The
   kernel and initrd stay in ``/casper/`` on the ISO 9660 tree; U-Boot reads
   them directly via its isofs driver.
4. Strips the shim, GRUB and MOK manager binaries
   (``/EFI/boot/{bootx64,grubx64,mmx64}.efi``) from the ISO 9660 tree. The UEFI
   firmware loads ``BOOTX64.EFI`` from the appended ESP, so the ISO 9660 copies
   are unused dead weight. The BIOS El Torito image under ``/boot/grub/`` is
   left in place, so legacy-BIOS boot still chains into GRUB as before.
5. Wires BLS maintenance into the target system that a later ``Install Ubuntu``
   produces; see :ref:`target-bls-setup`.

Relevant options:

* ``-u PATH`` -- the U-Boot EFI application (required).
* ``-o PATH`` -- the output ISO (required).
* ``-a ARGS`` -- override the kernel command line written to
  ``loader/entry.conf``. When not given, the arguments after the first
  ``linux /casper/vmlinuz`` entry in the source ISO's ``/boot/grub/grub.cfg``
  are used verbatim, so the rewritten ISO boots exactly as the original. Pass
  something like ``console=ttyS0,115200 console=tty0 --- quiet splash`` when
  you want the kernel logged to serial too.
* ``-k PATH`` / ``-i PATH`` -- override the kernel and initrd paths inside the
  ISO if a distribution uses something other than ``casper/vmlinuz`` and
  ``casper/initrd``.
* ``-s MiB`` -- force an ESP size; the default is 4 MiB, enough for the U-Boot
  binary.
* ``-t TITLE`` -- override the BLS entry title.
* ``-N`` / ``--no-target-bls`` -- skip the :ref:`target-bls-setup` step and
  produce a plain rewritten ISO. Useful when you do not want ``squashfs-tools``
  / ``fakeroot`` on the host or cannot afford the extra ~3 minutes the squashfs
  repack adds.
* ``-I PATH`` / ``--install-squashfs PATH`` -- path inside the ISO of the
  install squashfs to modify for interactive installs. Defaults to
  ``casper/minimal.squashfs``; set to an empty string to skip the squashfs step
  while still shipping the autoinstall snippet.
* ``-v`` -- show progress markers and subprocess output (xorriso, mksquashfs,
  mkfs.vfat). Off by default.

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
``entry.conf`` lives in the ISO 9660 tree and U-Boot reads kernel and initrd
from the same partition via isofs.

.. _target-bls-setup:

Keeping BLS entries fresh after install
---------------------------------------

A rewritten live ISO boots via U-Boot + BLS, but once the user clicks
*Install Ubuntu*, apt-managed kernel updates still go through ``update-grub``
on the target. They do not refresh ``/boot/loader/entries/``, so the installed
system keeps booting the kernel the installer unpacked rather than whatever
``apt install linux-image-*`` lays down later.

By default the script reaches both possible install paths to seed BLS entries
for the installer's kernel:

1. **Autoinstall path.** ``/autoinstall.yaml`` at the ISO 9660 root carries
   ``late-commands`` that, after subiquity has finished copying the system to
   disk, write a BLS entry per kernel directly to
   ``/boot/efi/loader/entries/<v>.conf`` on the install target, copying the
   kernel and initrd alongside on the ESP so the entry can reference them by
   ESP-relative paths. A ``/loader/entry.conf`` fallback is written too,
   matching the format the live ISO already uses. Plain interactive installs
   ignore ``/autoinstall.yaml`` entirely, so shipping the file is harmless when
   autoinstall is not requested.

   The ``kernel-install`` round-trip used by other distros is bypassed: inside
   curtin's chroot, ``/boot/efi`` is a plain directory rather than the ESP
   mountpoint, and ``systemd``'s ``90-loaderentry.install`` plugin silently
   exits when it cannot find a real ESP under ``$BOOT_ROOT``.

   Entries land on the ESP rather than on ``/boot/loader/entries/`` on the
   rootfs because the bootflow iterator advances per-partition. On the install
   target the ESP is the lower-numbered partition; without an entry there the
   EFI bootmeth would match ``/EFI/BOOT/BOOTX64.EFI`` before the BLS bootmeth
   ever reached the rootfs further down, chain-loading U-Boot into itself.
   Putting the entry on the ESP lets the BLS bootmeth win on that partition
   before the EFI bootmeth has a turn. A separately maintained
   ``/boot/loader/entries/`` on the rootfs would still be picked up (the BLS
   bootmeth scans every partition); this just guarantees a working entry on
   the partition the iterator reaches first.

2. **Interactive path.** The script unpacks the default install squashfs
   (``casper/minimal.squashfs``), drops in a ``oneshot`` systemd unit plus its
   helper script and a copy of ``u-boot.efi`` at ``/usr/lib/u-boot/u-boot.efi``,
   and repacks. On first boot of the installed system the unit runs
   ``apt install systemd-boot-efi``, back-fills ``/boot/loader/entries/`` via
   ``kernel-install``, and installs ``u-boot.efi`` onto the target ESP (see
   :ref:`target-uboot-install` below).

   The unit is gated on ``ConditionPathExists=!/cdrom`` so it stays dormant
   inside the live session, and the done-file stops it re-running on subsequent
   boots. It needs a network on first boot for the apt call; if that is not
   available it fails gracefully and retries on the next boot.

.. _target-uboot-install:

Installing U-Boot onto the target ESP
-------------------------------------

Subiquity writes shim and GRUB to the installed ESP and registers an ``ubuntu``
NVRAM entry pointing at ``/EFI/ubuntu/shimx64.efi``. Without further action,
firmware prefers that entry over the removable-media fallback, so control leaves
U-Boot as soon as the install reboots.

Both install paths therefore plant ``u-boot.efi`` on the target ESP at two
locations:

* ``/EFI/BOOT/BOOTX64.EFI`` -- the firmware fallback, picked up when no NVRAM
  entry matches (for example after a CMOS reset).
* ``/EFI/ubuntu/shimx64.efi`` -- the file the ``ubuntu`` NVRAM entry points at,
  overwritten in place so the entry boots U-Boot rather than chaining through
  shim into GRUB.

The autoinstall path does the copy in its ``late-commands`` via
``curtin in-target``; the interactive first-boot unit does it from the running
installed system. Either way ``u-boot.efi`` travels through the install inside
the modified squashfs at ``/usr/lib/u-boot/u-boot.efi``, so the installed disk
boots U-Boot + BLS on its own once the live ISO has been ejected.

For interactive installs, every subsequent ``apt install linux-image-*`` on the
target updates ``/boot/loader/entries/`` automatically via the
``kernel-install`` hooks, and U-Boot's BLS bootmeth keeps picking up the current
kernel/initrd without regenerating the ISO. Autoinstall systems ship with one
entry per kernel the installer unpacked but do not auto-refresh on later kernel
updates -- the autoinstall path is intended to get the freshly installed system
to its first boot under U-Boot, not to replace ``kernel-install``. Run the
interactive path's first-boot unit (or ``kernel-install add`` manually)
afterwards if you want apt-driven kernel updates to maintain BLS entries on an
autoinstalled system.

Pass ``-N`` / ``--no-target-bls`` to skip both steps and produce a plain
rewritten ISO. The squashfs repack is the slow part of the build (about three
extra minutes on the 24.04.1 desktop ISO); skipping it keeps the rewrite under
a minute at the cost of needing a manual ``kernel-install add`` on the
installed system.

See also
--------

* :doc:`/usage/bls` -- the U-Boot Boot Loader Specification bootmeth.
* `Boot Loader Specification
  <https://uapi-group.org/specifications/specs/boot_loader_specification/>`_.
* ``kernel-install(8)`` -- systemd's kernel install hook.
