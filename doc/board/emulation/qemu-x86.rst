.. SPDX-License-Identifier: GPL-2.0+
.. sectionauthor:: Bin Meng <bmeng.cn@gmail.com>

QEMU x86
========

Build instructions for bare mode
--------------------------------

To build u-boot.rom for QEMU x86 targets, just simply run::

   $ make qemu-x86_defconfig (for 32-bit)
   $ make qemu-x86_64_defconfig (for 64-bit)
   $ make all

Note this default configuration will build a U-Boot for the QEMU x86 i440FX
board. To build a U-Boot against QEMU x86 Q35 board, you can change the build
configuration during the 'make menuconfig' process like below::

   Device Tree Control  --->
       ...
       (qemu-x86_q35) Default Device Tree for DT control

Test with QEMU for bare mode
----------------------------

QEMU is a fancy emulator that can enable us to test U-Boot without access to
a real x86 board. Please make sure your QEMU version is 2.3.0 or above test
U-Boot. To launch QEMU with u-boot.rom, call QEMU as follows::

   $ qemu-system-i386 -nographic -bios path/to/u-boot.rom

This will instantiate an emulated x86 board with i440FX and PIIX chipset. QEMU
also supports emulating an x86 board with Q35 and ICH9 based chipset, which is
also supported by U-Boot. To instantiate such a machine, call QEMU with::

   $ qemu-system-i386 -nographic -bios path/to/u-boot.rom -M q35

Note by default QEMU instantiated boards only have 128 MiB system memory. But
it is enough to have U-Boot boot and function correctly. You can increase the
system memory by pass '-m' parameter to QEMU if you want more memory::

   $ qemu-system-i386 -nographic -bios path/to/u-boot.rom -m 1024

This creates a board with 1 GiB system memory. Currently U-Boot for QEMU only
supports 3 GiB maximum system memory and reserves the last 1 GiB address space
for PCI device memory-mapped I/O and other stuff, so the maximum value of '-m'
would be 3072.

QEMU emulates a graphic card which U-Boot supports. Removing '-nographic' will
show QEMU's VGA console window. Note this will disable QEMU's serial output.
If you want to check both consoles, use '-serial stdio'.

Multicore is also supported by QEMU via '-smp n' where n is the number of cores
to instantiate. Note, the maximum supported CPU number in QEMU is 255.

U-Boot uses 'distro_bootcmd' by default when booting on x86 QEMU. This tries to
load a boot script, kernel, and ramdisk from several different interfaces. For
the default boot order, see 'qemu-x86.h'. For more information, see
'doc/develop/distro.rst'. Most Linux distros can be booted by writing a uboot
script.
For example, Debian (stretch) can be booted by creating a script file named
'boot.txt' with the contents::

   env set bootargs root=/dev/sda1 ro
   load ${devtype} ${devnum}:${distro_bootpart} ${kernel_addr_r} /vmlinuz
   load ${devtype} ${devnum}:${distro_bootpart} ${ramdisk_addr_r} /initrd.img
   zboot ${kernel_addr_r} - ${ramdisk_addr_r} ${filesize}

Then compile and install it with::

   $ apt install u-boot-tools && \
     mkimage -T script -C none -n "Boot script" -d boot.txt /boot/boot.scr

The fw_cfg interface in QEMU also provides information about kernel data,
initrd, command-line arguments and more. U-Boot supports directly accessing
these informtion from fw_cfg interface, which saves the time of loading them
from hard disk or network again, through emulated devices. To use it , simply
providing them in QEMU command line::

   $ qemu-system-i386 -nographic -bios path/to/u-boot.rom -m 1024 \
     -kernel /path/to/bzImage -append 'root=/dev/ram console=ttyS0' \
     -initrd /path/to/initrd -smp 8

Note: -initrd and -smp are both optional

Then start QEMU, in U-Boot command line use the following U-Boot command to
setup kernel::

   => qfw
   qfw - QEMU firmware interface

   Usage:
   qfw <command>
       - list                             : print firmware(s) currently loaded
       - cpus                             : print online cpu number
       - load <kernel addr> <initrd addr> : load kernel and initrd (if any) and setup for zboot

   => qfw load
   loading kernel to address 01000000 size 5d9d30 initrd 04000000 size 1b1ab50

Here the kernel (bzImage) is loaded to 01000000 and initrd is to 04000000. Then,
'zboot' can be used to boot the kernel::

   => zboot 01000000 - 04000000 1b1ab50

To run 64-bit U-Boot, qemu-system-x86_64 should be used instead, e.g.::

   $ qemu-system-x86_64 -nographic -bios path/to/u-boot.rom

A specific CPU can be specified via the '-cpu' parameter but please make
sure the specified CPU supports 64-bit like '-cpu core2duo'. Conversely
'-cpu pentium' won't work for obvious reasons that the processor only
supports 32-bit.

Specifying a boot command
--------------------------

See :doc:`common` for details on how to provide a boot command to U-Boot on
startup.

Booting distros
---------------

It is possible to install and boot a standard Linux distribution using
qemu-x86_64 by setting up a root disk::

   qemu-img create root.img 10G

then using the installer to install. For example, with Ubuntu 2023.04::

   qemu-system-x86_64 -m 8G -smp 4 -bios /tmp/b/qemu-x86_64/u-boot.rom \
     -drive file=root.img,if=virtio,driver=raw \
     -drive file=ubuntu-23.04-desktop-amd64.iso,if=virtio,driver=raw

You can also add `-serial mon:stdio` if you want the serial console to show as
well as the video.

The output will be something like this::

   U-Boot SPL 2023.07 (Jul 23 2023 - 08:00:12 -0600)
   Trying to boot from SPI
   Jumping to 64-bit U-Boot: Note many features are missing


   U-Boot 2023.07 (Jul 23 2023 - 08:00:12 -0600)

   CPU:   QEMU Virtual CPU version 2.5+
   DRAM:  8 GiB
   Core:  20 devices, 13 uclasses, devicetree: separate
   Loading Environment from nowhere... OK
   Model: QEMU x86 (I440FX)
   Net:   e1000: 52:54:00:12:34:56
          eth0: e1000#0
   Hit any key to stop autoboot:  0
   Scanning for bootflows in all bootdevs
   Seq  Method       State   Uclass    Part  Ent  Name                      Filename
   ---  -----------  ------  --------  ----  ---  ------------------------  ----------------
   Scanning global bootmeth 'efi_mgr':
   Hunting with: nvme
   Hunting with: qfw
   Hunting with: scsi
   scanning bus for devices...
   Hunting with: virtio
   Scanning bootdev 'qfw_pio.bootdev':
   fatal: no kernel available
   Scanning bootdev 'virtio-blk#0.bootdev':
   Scanning bootdev 'virtio-blk#1.bootdev':
     0  efi          ready   virtio       2  virtio-blk#1.bootdev.part efi/boot/bootx64.efi
   ** Booting bootflow 'virtio-blk#1.bootdev.part_2' with efi
   EFI using ACPI tables at f0060
        efi_install_fdt() WARNING: Can't have ACPI table and device tree - ignoring DT.
          efi_run_image() Booting /efi\boot\bootx64.efi
   error: file `/boot/' not found.

Standard boot looks through various available devices and finds the virtio
disks, then boots from the first one. After a second or so the grub menu appears
and you can work through the installer flow normally.

Note that standard boot will not find 32-bit distros, since it looks for a
different filename.

EFI variables
-------------

On qemu-x86_64, U-Boot keeps its EFI variables in a flash, which QEMU
provides as pflash. QEMU only takes a variable flash behind a code flash, so
the ROM goes in as pflash unit 0 and the variables as unit 1, a 512 KiB image
which then sits just below the ROM, at 0xffd80000::

   qemu-img create -f raw vars.img 512K
   qemu-system-x86_64 -M q35 -m 4G -enable-kvm -nographic \
     -drive if=pflash,unit=0,file=/tmp/b/qemu-x86_64/u-boot.rom,format=raw,readonly=on \
     -drive if=pflash,unit=1,file=vars.img,format=raw

Variables set at boot time, and at runtime by the OS, are written to the
flash at once and so survive a cold boot. Without the flash, U-Boot keeps
them in memory, where they last for a warm reset only.

Booting Windows
---------------

Windows 10 can be installed and booted in the same way. Its boot manager
needs nothing beyond what U-Boot provides. Windows setup expects its install
media to be a CD-ROM and Windows has drivers for SATA but not for virtio, so
use the q35 machine and put both the disk and the ISO on its SATA
controller::

   qemu-img create -f qcow2 win.qcow2 32G
   qemu-system-x86_64 -M q35 -m 4G -smp 4 -enable-kvm \
     -bios /tmp/b/qemu-x86_64/u-boot.rom \
     -drive if=none,id=hd0,file=win.qcow2 -device ide-hd,drive=hd0,bus=ide.0 \
     -drive if=none,id=cd0,media=cdrom,file=Win10.iso,format=raw,readonly=on \
     -device ide-cd,drive=cd0,bus=ide.1 \
     -serial mon:stdio

Virtio disks are faster: U-Boot reaches the kernel about 0.6 s sooner from a
virtio disk and 2 s sooner from a virtio ISO than through AHCI. Windows has
no virtio driver of its own, though, so setup must be given the ``viostor``
driver from the virtio-win ISO, through the setup UI or ``DriverPaths`` in
an ``autounattend.xml``, and it still wants the install media on a CD-ROM,
so the ISO is attached both ways::

   qemu-system-x86_64 -M q35 -m 4G -smp 4 -enable-kvm \
     -bios /tmp/b/qemu-x86_64/u-boot.rom \
     -drive if=none,id=hd0,file=win.qcow2 -device virtio-blk-pci,drive=hd0 \
     -drive if=none,id=iso0,file=Win10.iso,format=raw,readonly=on \
     -device virtio-blk-pci,drive=iso0 \
     -drive if=none,id=cd0,media=cdrom,file=Win10.iso,format=raw,readonly=on \
     -device ide-cd,drive=cd0,bus=ide.1 \
     -drive if=none,id=cd1,media=cdrom,file=virtio-win.iso,format=raw,readonly=on \
     -device ide-cd,drive=cd1,bus=ide.2 \
     -serial mon:stdio

Once installed that way, Windows boots from the virtio disk alone.

The boot manager asks for a key press before it will boot from the
installer; the serial console works for that. Setup then runs as it would on
any other machine and reboots a few times; each time U-Boot's boot manager
starts Windows from the disk. The whole install takes about six minutes on a
fast host.

At the end of the first phase, setup writes its boot entries with the runtime
SetVariable() service and refuses to continue if that fails, so
``CONFIG_EFI_RT_VOLATILE_STORE`` is enabled for QEMU. With the variable flash
attached those entries go straight to the flash; without it U-Boot keeps them
in memory, which is enough for setup, since on the next boot the boot manager
finds Windows through the entries U-Boot generates for each disk.

Windows writes nothing to the serial port at all, so once the kernel starts
the video console is the only way to see it.

Current limitations
-------------------

Only qemu-x86-64 can be used for booting distros, since qemu-x86 (the 32-bit
version of U-Boot) seems to have an EFI bug leading to the boot handing after
Linux is selected from grub, e.g. with `debian-12.1.0-i386-netinst.iso`::

   ** Booting bootflow 'virtio-blk#1.bootdev.part_2' with efi
   EFI using ACPI tables at f0180
        efi_install_fdt() WARNING: Can't have ACPI table and device tree - ignoring DT.
          efi_run_image() Booting /efi\boot\bootia32.efi
   Failed to open efi\boot\root=/dev/sdb3 - Not Found
   Failed to load image 큀緃: Not Found
   start_image() returned Not Found, falling back to default loader
   Welcome to GRUB!

The bochs video driver also seems to cause problems before the OS is able to
show a display.

The QEMU `-cdrom` option is intended to work with the original ISO-format
images, not the recently invented ISOHybrid image.

Finally, the use of `-M accel=kvm` is intended to use the native CPU's
virtual-machine features to accelerate operation, but this causes U-Boot to hang
when jumping 64-bit mode, at least on AMD machines. This may be a bug in U-Boot
or something else.
