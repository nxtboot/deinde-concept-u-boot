# SPDX-License-Identifier: GPL-2.0+
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>

import pytest
import utils

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
