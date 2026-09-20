# SPDX-License-Identifier: GPL-2.0+
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>

import pytest
import utils

from console_base import Timeout

# Enable early console so that the test can see if something goes wrong
CONSOLE = 'earlycon=uart8250,io,0x3f8 console=uart8250,io,0x3f8'

@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64')
@pytest.mark.restart
def test_distro(ubman):
    """Test booting into Ubuntu 24.04"""
    with ubman.log.section('boot'):
        ubman.run_command('boot', wait_for_prompt=False)

    with ubman.log.section('Grub'):
        # Wait for grub to come up and offset a menu
        ubman.expect(['Try or Install Ubuntu'])

        # Press 'e' to edit the command line
        ubman.log.info("Pressing 'e'")
        ubman.run_command('e', wait_for_prompt=False, send_nl=False)

        # Wait until we see the editor appear
        ubman.expect(['/casper/initrd'])

        # Go down to the 'linux' line. Avoid using down-arrow as that includes
        # an Escape character, which may be parsed by Grub as such, causing it
        # to return to the top menu
        ubman.log.info("Going DOWN")
        ubman.ctrl('N')
        ubman.ctrl('N')
        ubman.ctrl('N')

        # Go to end of line
        ubman.log.info("Going to EOL")
        ubman.ctrl('E')

        # Backspace to remove 'quiet splash'
        ubman.log.info("Erasing quiet and splash")
        ubman.send('\b' * len('quiet splash'))

        # Send our noisy console
        ubman.log.info("Noisy console")
        ubman.send(CONSOLE)

        # Tell grub to boot
        ubman.log.info("boot")
        ubman.ctrl('X')
        ubman.expect(['Booting a command list'])

    with ubman.log.section('Linux'):
        # Linux should start immediately
        ubman.expect(['Linux version'])

    with ubman.log.section('Ubuntu'):
        # Shortly later, we should see this banner
        with ubman.temporary_timeout(200 * 1000):
            ubman.expect(['Welcome to .*Ubuntu 24.04.1 LTS.*!'])

    ubman.restart_uboot()

@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win')
@pytest.mark.restart
def test_distro_windows(ubman):
    """Boot the Windows 10 installer ISO through U-Boot's EFI loader

    The 'qemu-x86_64-win' role attaches the Windows ISO as a virtio disk.
    U-Boot's ISO_PARTITION support exposes the El Torito boot image as a FAT
    partition and the efi bootmeth runs its EFI/BOOT/BOOTX64.EFI, which is the
    Windows boot manager. That asks on the console before it will boot from an
    optical disc, and gives up with EFI_TIMEOUT if nobody answers, so answer
    it. Then wait for U-Boot's ExitBootServices() report, which shows that the
    boot manager and winload have read the kernel, HAL, drivers and boot.wim
    (several hundred MB over the block-IO protocol) and handed over to the
    kernel. Windows itself writes nothing to the serial port, so that is the
    last thing the test can see.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('bootmgr'):
        ubman.expect(['Press any key to boot from CD or DVD'])
        ubman.send('\r')

    with ubman.log.section('winload'):
        with ubman.temporary_timeout(180 * 1000):
            ubman.expect(['Starting kernel'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win-installed')
@pytest.mark.restart
def test_distro_windows_installed(ubman):
    """Boot an installed Windows 10 through U-Boot's EFI loader

    The 'qemu-x86_64-win-installed' role attaches a disk holding a Windows
    10 installation (UEFI/GPT, with the virtio-win drivers) as a virtio
    disk, in snapshot mode so that the image is never modified. U-Boot finds
    the ESP
    and runs EFI/Boot/bootx64.efi, which is the Windows boot manager; that
    reads the BCD and loads winload, the kernel and the boot drivers from the
    NTFS volume over the block-IO protocol, then calls ExitBootServices().

    Windows itself writes nothing to the serial port, so the image has a
    logon script (installed by its autounattend.xml) which prints a marker to
    COM1 each time a user logs on, and the account logs on automatically.
    Seeing that marker means the kernel came up from the NTFS volume, the
    drivers loaded and the desktop is there, which takes a few minutes on
    KVM.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('winload'):
        with ubman.temporary_timeout(180 * 1000):
            ubman.expect(['Starting kernel'])

    with ubman.log.section('Windows'):
        with ubman.temporary_timeout(600 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win-install')
@pytest.mark.restart
def test_distro_windows_install(ubman):
    """Install Windows 10 from its ISO with U-Boot as the firmware

    The 'qemu-x86_64-win-install' role attaches a blank virtio disk in
    snapshot mode as the target and the installer ISO as a second virtio
    disk for U-Boot to boot from, plus the same ISO and an unattend CD (with
    the virtio-win drivers, which setup needs to see the target) as SATA
    CD-ROMs for Windows setup, which only reads its media from a CD-ROM.

    On the first boot the blank disk has nothing to boot, so the boot manager
    falls through to the ISO and asks for a key press. Setup then runs
    unattended, writing its boot entries through the runtime SetVariable()
    service, and reboots several times; each time U-Boot boots the disk.
    The unattend file's first-logon script prints a marker to COM1, which is
    the sign that the installation completed and Windows logged on. It then
    powers the VM off.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('bootmgr'):
        ubman.expect(['Press any key to boot from CD or DVD'])
        ubman.send('\r')

    with ubman.log.section('setup'):
        with ubman.temporary_timeout(180 * 1000):
            ubman.expect(['Starting kernel'])

    # Setup copies Windows, reboots into the specialize and OOBE passes and
    # finally logs on, which is when the marker appears
    with ubman.log.section('install'):
        with ubman.temporary_timeout(20 * 60 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win11-installed')
@pytest.mark.restart
def test_distro_windows11_installed(ubman):
    """Boot an installed Windows 11 through U-Boot's EFI loader

    As test_distro_windows_installed(), for Windows 11: the
    'qemu-x86_64-win11-installed' role attaches the installation as a SATA
    disk in snapshot mode and gives the machine a TPM 2.0 (through swtpm),
    which Windows 11 expects to find, along with the EFI variable flash.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('winload'):
        with ubman.temporary_timeout(180 * 1000):
            ubman.expect(['Starting kernel'])

    with ubman.log.section('Windows'):
        with ubman.temporary_timeout(600 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu_arm64_acpi')
@pytest.mark.role('qemu-arm64-win11-installed')
@pytest.mark.restart
def test_distro_windows11_arm64_installed(ubman):
    """Boot an installed Windows 11 Arm64 through U-Boot's EFI loader

    As test_distro_windows11_installed(), on QEMU's Arm 'virt' machine: the
    'qemu-arm64-win11-installed' role attaches the installation as an NVMe
    disk in snapshot mode, with a TPM 2.0, and U-Boot hands Windows the ACPI
    tables which QEMU provides.

    Windows on Arm never offers the PL011 as a COM port, so the marker comes
    over an FTDI USB serial port, which the role adds and which shares the
    console; the image has FTDI's driver installed. There is no Arm host in
    the lab, so the machine is emulated and the boot takes a few minutes.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(120 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('winload'):
        with ubman.temporary_timeout(300 * 1000):
            ubman.expect(['Starting kernel'])

    with ubman.log.section('Windows'):
        with ubman.temporary_timeout(1200 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win11-install')
@pytest.mark.restart
def test_distro_windows11_install(ubman):
    """Install Windows 11 from its ISO with U-Boot as the firmware

    As test_distro_windows_install(), for Windows 11, which adds two things.
    Setup refuses to install without a TPM 2.0, so the
    'qemu-x86_64-win11-install' role gives the machine one through swtpm,
    which U-Boot's TCG2 protocol serves to the boot manager. And that boot
    manager takes the key press for booting from the CD without ever showing
    the prompt on the console, so the test presses Enter every second for a
    while after the boot manager starts rather than waiting for the prompt.
    Everything is on SATA, which needs no drivers from Windows' side.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('bootmgr'):
        for _ in range(30):
            try:
                with ubman.temporary_timeout(1000):
                    ubman.expect(['Starting kernel'])
                break
            except Timeout:
                ubman.send('\r')
        else:
            with ubman.temporary_timeout(180 * 1000):
                ubman.expect(['Starting kernel'])

    # Setup copies Windows, reboots into the specialize and OOBE passes and
    # finally logs on, which is when the marker appears
    with ubman.log.section('install'):
        with ubman.temporary_timeout(20 * 60 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])

    ubman.restart_uboot()


@pytest.mark.boardspec('qemu-x86_64')
@pytest.mark.role('qemu-x86_64-win11-sb')
@pytest.mark.restart
def test_distro_windows11_secure(ubman):
    """Boot Windows 11 with secure boot enabled

    As test_distro_windows11_installed(), but the 'qemu-x86_64-win11-sb'
    role's variable flash holds a platform key with Microsoft's KEK and db
    certificates, so U-Boot is in secure-boot mode and verifies the boot
    manager's signature against db before running it. The image's logon
    script also reports what Windows made of it: its kernel records secure
    boot as enabled in the registry (UEFISecureBootEnabled), which is what
    msinfo32 shows as the Secure Boot State.
    """
    with ubman.log.section('boot'):
        with ubman.temporary_timeout(60 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with efi"])

    with ubman.log.section('winload'):
        with ubman.temporary_timeout(180 * 1000):
            ubman.expect(['Starting kernel'])

    with ubman.log.section('Windows'):
        with ubman.temporary_timeout(600 * 1000):
            ubman.expect(['UBOOT-WINDOWS-LOGON'])
            ubman.expect([r'UBOOT-WINDOWS-SECUREBOOT [^\r\n]*reg=1'])

    ubman.restart_uboot()


@pytest.mark.boardspec('colibri-imx8x')
@pytest.mark.role('colibrimx8')
@pytest.mark.restart
def test_distro_script(ubman):
    """Test that a selected board can boot into Llinux using a script"""
    with ubman.log.section('boot'):
        ubman.run_command('boot', wait_for_prompt=False)

    # This is the start of userspace
    ubman.expect(['Welcome to TDX Wayland'])

    # Shortly later, we should see this banner
    ubman.expect(['Colibri-iMX8X_Reference-Multimedia-Image'])

    ubman.restart_uboot()

@pytest.mark.boardspec('efi-arm_app64')
@pytest.mark.role('efi-aarch64')
@pytest.mark.restart
def test_distro_arm_app_extlinux(ubman):
    """Test that the ARM EFI app can boot into Ubuntu 25.04 via extlinux"""
    with ubman.log.section('boot'):
        ubman.run_command('bootmeth order extlinux')
        ubman.run_command('boot', wait_for_prompt=False)

        ubman.expect(["Booting bootflow 'efi_media_1.bootdev.part_2' with extlinux"])
        ubman.expect(['Exiting EFI'])
        ubman.expect(['Booting Linux on physical CPU'])

    with ubman.log.section('initrd'):
        with ubman.temporary_timeout(200 * 1000):
            ubman.expect(['Starting systemd-udevd'])
            ubman.expect(['Welcome to Ubuntu 25.04!'])

    ubman.restart_uboot()

@pytest.mark.boardspec('efi-x86_app64')
@pytest.mark.role('efi-x86_64-uboot-iso-install')
@pytest.mark.restart
def test_distro_ubuntu_iso_install(ubman):
    """Run an unattended Ubuntu install through U-Boot + BLS.

    Needs a role whose UBootWriterDriver passes --autoinstall to
    scripts/ubuntu-iso-to-uboot.py and whose QEMU config attaches a blank virtio
    disk as the install target. The rewritten ISO boots straight into subiquity
    (autoinstall on the BLS cmdline), subiquity partitions the target disk and
    installs Ubuntu, then kernel-install populates /boot/loader/entries/ on the
    installed ESP. On reboot, OVMF hands control back to U-Boot (still living on
    the ISO ESP), whose BOOTMETH_BLS picks up the new entries from the installed
    disk and boots that system.
    """
    with ubman.log.section('boot-installer'):
        with ubman.temporary_timeout(120 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with bls"])
            ubman.expect(['Linux version'])

    # Subiquity sends its reporting events through journald; with
    # forward_to_console=1 on the cmdline they appear on ttyS0. Each event is
    # logged as 'start: <path>: <description>' (or 'finish: ...'), so wait for
    # the start of the curtin_install step, which follows partitioning and
    # marks the point where curtin begins copying the system to disk.
    with ubman.log.section('subiquity-install'):
        with ubman.temporary_timeout(10 * 60 * 1000):
            ubman.expect(
                [r'start: +subiquity/Install/install/curtin_install:'])

    # The clean end-of-install marker: subiquity finishes, systemd tears the
    # session down and the kernel prints 'Restarting system' on the way out.
    # 'quiet' has been stripped so this is visible.
    with ubman.log.section('subiquity-reboot'):
        with ubman.temporary_timeout(45 * 60 * 1000):
            ubman.expect([r'reboot: Restarting system'])

    # After reboot OVMF re-runs U-Boot from the ISO ESP. BLS now sees the
    # installed disk's /loader/entries/ and should pick the newly written entry
    # rather than the ISO's casper one.
    with ubman.log.section('boot-installed'):
        with ubman.temporary_timeout(300 * 1000):
            ubman.expect([r"Booting bootflow '[^']+' with bls"])
            ubman.expect(['Linux version'])

    with ubman.log.section('installed-ubuntu'):
        with ubman.temporary_timeout(300 * 1000):
            ubman.expect([r' login: '])

    ubman.restart_uboot()


@pytest.mark.boardspec('efi-x86_app64')
@pytest.mark.role('efi-x86_64-uboot-iso')
@pytest.mark.restart
def test_distro_ubuntu_iso_uboot(ubman):
    """Boot a rewritten Ubuntu live ISO through U-Boot + BLS.

    The 'efi-x86_64-uboot-iso' role's UBootWriterDriver uses the
    'qemu-efi-iso' method, which runs scripts/ubuntu-iso-to-uboot.py
    to rebuild the destination ISO (image slot 'ubuntu-uboot') from
    the source (slot 'ubuntu') before QEMU boots. U-Boot's
    BOOTMETH_BLS then picks up /loader/entry.conf and loads the
    casper kernel/initrd straight off the ISO 9660 partition.
    """
    with ubman.log.section('boot'):
        # The efi_media_0 phantom bootdev OVMF exposes makes bootflow
        # scan iterate through many dead probes before reaching the
        # ISO's real partition, so allow a generous timeout for the
        # first BLS match.
        with ubman.temporary_timeout(120 * 1000):
            ubman.run_command('boot', wait_for_prompt=False)
            ubman.expect([r"Booting bootflow '[^']+' with bls"])

    # The ISO's cmdline carries 'quiet splash' so kernel-level info
    # messages (including systemd-journald's kmsg output) are dropped;
    # what does reach ttyS0 is systemd-pid1's own '[  OK  ]' status
    # writer. gdm.service is the latest reliable "userspace fully up"
    # marker before casper's snap-run race loop fills the console.
    with ubman.log.section('Ubuntu'):
        with ubman.temporary_timeout(120 * 1000):
            ubman.expect([r'Started gdm\.service'])

    ubman.restart_uboot()

@pytest.mark.boardspec('efi-arm_app64')
@pytest.mark.role('efi-aarch64')
@pytest.mark.restart
def test_distro_arm_app_efi(ubman):
    """Test that the ARM EFI app can boot into Ubuntu 25.04 via EFI"""
    with ubman.log.section('boot'):
        ubman.run_command('bootmeth order efi')
        ubman.run_command('boot', wait_for_prompt=False)

        ubman.expect(
            ["Booting bootflow 'efi_media_1.bootdev.part_1' with efi"])

    # Wait for Linux to boot to userspace (kernel may be quiet)
    with ubman.log.section('Linux'):
        with ubman.temporary_timeout(200 * 1000):
            ubman.expect(['Ubuntu 25.04 qarm ttyAMA0'])

    ubman.restart_uboot()
