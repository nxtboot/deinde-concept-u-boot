#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
"""Turn an Ubuntu live ISO into one that boots via U-Boot + BLS.

BLS (Boot Loader Specification) is a freedesktop.org standard for
describing boot menu entries as small files under /loader/entries/ on
the EFI system partition. Each entry names a kernel, initrd and command
line; U-Boot's BOOTMETH_BLS scans the ESP and presents them in its boot
menu, replacing the shim/grub chain that Ubuntu ships by default.

Two things change in the rewritten ISO:

    EFI system partition  /EFI/BOOT/BOOTX64.EFI  replaced with the U-Boot
                                                 EFI app
    ISO 9660 tree         /loader/entry.conf     added, pointing at the
                                                 existing /casper/vmlinuz
                                                 and /casper/initrd

The kernel and initrd stay where Ubuntu put them. U-Boot reads them off
the ISO 9660 partition directly via its isofs driver. All other boot
records (BIOS El Torito, grub2 MBR, GPT layout) are preserved by
xorriso's -boot_image any replay.

Quick start (run from the root of the U-Boot tree)::

    # 1. Install host tools
    sudo apt install xorriso mtools dosfstools qemu-system-x86 ovmf

    # 2. Download an Ubuntu live ISO (desktop or server both work)
    curl -LO https://releases.ubuntu.com/24.04.1/ubuntu-24.04.1-desktop-amd64.iso

    # 3. Build U-Boot as an x86_64 EFI application. The defconfig enables
    #    BOOTMETH_BLS, FS_ISOFS and JOLIET. If rustc is not installed,
    #    also pass -d RUST_EXAMPLES -d EXAMPLES to scripts/config and
    #    re-run olddefconfig before building.
    make O=/tmp/b/efi-x86_app64 efi-x86_app64_defconfig
    make O=/tmp/b/efi-x86_app64 -j$(nproc)
    # produces /tmp/b/efi-x86_app64/u-boot-app.efi

    # 4. Rewrite the ISO to boot via U-Boot
    scripts/ubuntu-iso-to-uboot.py ubuntu-24.04.1-desktop-amd64.iso \\
        -u /tmp/b/efi-x86_app64/u-boot-app.efi \\
        -o ubuntu-uboot.iso

    # 5. Try it under QEMU + OVMF
    cp /usr/share/OVMF/OVMF_VARS_4M.fd /tmp/OVMF_VARS.fd
    qemu-system-x86_64 -machine q35 -m 4096 -smp 2 \\
        -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \\
        -drive if=pflash,format=raw,file=/tmp/OVMF_VARS.fd \\
        -drive if=virtio,file=ubuntu-uboot.iso,format=raw,readonly=on

Assumptions:
    - Ubuntu-style live ISO with casper/vmlinuz and casper/initrd.
    - Input ISO has an appended GPT EFI System Partition
      (-append_partition 2 in xorriso's report).
    - U-Boot EFI app is built with CONFIG_BOOTMETH_BLS=y,
      CONFIG_CMD_ZBOOT=y and FAT support (efi-x86_app64_defconfig is
      the reference config).
    - xorriso, mtools and dosfstools are installed on the host
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Add the tools directory to the path for u_boot_pylib
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

# pylint: disable=wrong-import-position,import-error
from u_boot_pylib import command
from u_boot_pylib import tout

REQUIRED_TOOLS = (
    'xorriso', 'mcopy', 'mmd', 'mkfs.vfat',
    'unsquashfs', 'mksquashfs', 'fakeroot',
)
MIB = 1024 * 1024

# First-boot systemd unit + helper script written into the install
# squashfs so kernel-install manages BLS entries on the installed
# system. ConditionPathExists=!/cdrom keeps the unit dormant inside
# the live session, and the done-file stops it re-running on every
# subsequent boot.
FIRST_BOOT_SCRIPT = '''\
#!/bin/sh
# Set up BLS entries and install U-Boot on the target ESP so the
# installed system boots via U-Boot + BLS without the live ISO
set -e
apt-get update
apt-get install -y systemd-boot-efi
mkdir -p /boot/loader/entries
# Force kernel-install's BLS layout: with the default layout=auto
# the heuristic can pick 'other' on Ubuntu and skip the
# 90-loaderentry.install plugin
grep -q '^layout=' /etc/kernel/install.conf 2>/dev/null \\
	|| echo layout=bls >> /etc/kernel/install.conf
for k in /usr/lib/modules/*; do
	v=$(basename "$k")
	kernel-install add "$v" "/boot/vmlinuz-$v" || true
done
# Plant u-boot.efi on the installed ESP. BOOTX64.EFI is the firmware
# fallback; shimx64.efi is what the 'ubuntu' NVRAM entry Subiquity
# registers points at, so overwriting both means the disk boots
# U-Boot whichever path the firmware takes.
UBOOT=/usr/lib/u-boot/u-boot.efi
install -D -m 644 "$UBOOT" /boot/efi/EFI/BOOT/BOOTX64.EFI
if [ -f /boot/efi/EFI/ubuntu/shimx64.efi ]; then
	install -m 644 "$UBOOT" /boot/efi/EFI/ubuntu/shimx64.efi
fi
touch /var/lib/ubuntu-iso-to-uboot-bls-setup.done
'''

FIRST_BOOT_UNIT = '''\
[Unit]
Description=Set up BLS entries for future kernel updates
ConditionPathExists=!/var/lib/ubuntu-iso-to-uboot-bls-setup.done
ConditionPathExists=!/cdrom
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/ubuntu-iso-to-uboot-bls-setup
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
'''


def _quiet() -> bool:
    """True when tout is set below INFO (no progress chatter requested)."""
    return tout.verbose < tout.INFO


def _run(*cmd) -> None:
    """Run a command, capturing its output when tout is quiet.

    On failure u_boot_pylib's CommandExc still carries the captured
    output, so the user sees what went wrong.
    """
    quiet = _quiet()
    command.run(*cmd, capture=quiet, capture_stderr=quiet)


def check_tools() -> None:
    missing = [t for t in REQUIRED_TOOLS if not shutil.which(t)]
    if missing:
        tout.fatal(
            f'missing tools: {" ".join(missing)}\n'
            f'try: apt install xorriso mtools dosfstools'
        )


def parse_grub_cmdline(iso: Path, kernel: str) -> str:
    """Return the kernel cmdline from the ISO's default grub entry.

    Parses the first `linux <kernel> ...` line in /boot/grub/grub.cfg
    and strips the kernel path, so the caller can pass the remaining tokens
    to the kernel (e.g. '--- quiet splash' on Ubuntu 24.04.1).
    """
    with tempfile.TemporaryDirectory(prefix='iso2uboot.grub.') as td:
        dst = Path(td) / 'grub.cfg'
        _run(
            'xorriso', '-osirrox', 'on', '-indev', str(iso),
            '-extract', '/boot/grub/grub.cfg', str(dst),
        )
        cfg = dst.read_text(errors='replace')

    # The kernel path comes from the user (or its default), so escape it
    # before splicing it into the regex.
    kernel_re = re.escape(kernel.lstrip('/'))
    m = re.search(
        rf'^\s*linux\s+\S*{kernel_re}\S*\s*(.*)$',
        cfg, re.MULTILINE,
    )
    if not m:
        tout.fatal(
            f'could not find a {kernel} linux entry in /boot/grub/grub.cfg'
        )
    # Collapse any run of whitespace to a single space
    return ' '.join(m.group(1).split())


def parse_boot_report(iso: Path) -> tuple[str, str]:
    """Return (volume_label, esp_partition_guid) from xorriso's mkisofs report.

    Raises SystemExit if the ISO does not have an appended EFI System
    Partition on slot 2
    """
    # xorriso prints the report on stdout (which we need to parse) and
    # progress/status on stderr (which we swallow unless -v was passed).
    report = command.run(
        'xorriso', '-indev', str(iso), '-report_el_torito', 'as_mkisofs',
        capture=True, capture_stderr=_quiet(),
    )

    m_vol = re.search(r"^-V '([^']*)'", report, re.MULTILINE)
    m_esp = re.search(
        r'^-append_partition 2 ([0-9a-fA-F]+) ', report, re.MULTILINE,
    )
    if not m_vol:
        tout.fatal('could not find volume label in xorriso report')
    if not m_esp:
        tout.fatal(
            'could not find appended partition 2 in xorriso report '
            '(is this an Ubuntu-style hybrid ISO?)'
        )
    return m_vol.group(1), m_esp.group(1)


def build_esp(esp_path: Path, size_mib: int, uboot_efi: Path) -> None:
    """Create a fresh FAT ESP containing only the U-Boot EFI application.

    Args:
        esp_path (Path): Output file to hold the new ESP image
        size_mib (int): Size of the ESP in mebibytes
        uboot_efi (Path): U-Boot EFI app to install as /EFI/BOOT/BOOTX64.EFI
    """
    with esp_path.open('wb') as f:
        f.truncate(size_mib * MIB)
    # FAT12 is enough for the small ESP we emit (just a U-Boot binary).
    _run('mkfs.vfat', '-F12', '-n', 'ESP', str(esp_path))
    _run('mmd', '-i', str(esp_path), '::EFI', '::EFI/BOOT')
    _run('mcopy', '-i', str(esp_path), str(uboot_efi),
         '::EFI/BOOT/BOOTX64.EFI')


def repack_iso(
    in_iso: Path, out_iso: Path, esp_img: Path, esp_guid: str,
    file_maps: list[tuple[Path, str]],
) -> None:
    """Stream the input ISO to a new ISO with the ESP and BLS entry replaced.

    -boot_image any replay preserves every other boot record (BIOS El Torito,
    grub2 MBR, GPT layout); only the bytes behind partition 2 are rewritten,
    plus each (src, dst) pair in @file_maps is added to the ISO 9660 tree,
    and the shim, GRUB and MokManager copies under /EFI/boot/ are removed
    since U-Boot supplies the UEFI boot path via the appended ESP. The
    BIOS El Torito path still uses /boot/grub/ so legacy boot continues
    to work.

    -find is tolerant of missing files: if a distribution does not ship
    one of these binaries, the call is a no-op.

    xorriso refuses to write to an existing non-empty file when -indev
    and -outdev differ (it would treat the outdev as a session to
    extend), so unlink any stale output first.
    """
    if out_iso.exists():
        out_iso.unlink()
    cmd = [
        'xorriso',
        '-indev', str(in_iso),
        '-outdev', str(out_iso),
        '-boot_image', 'any', 'replay',
        '-append_partition', '2', esp_guid, str(esp_img),
    ]
    for src, dst in file_maps:
        cmd += ['-map', str(src), dst]
    cmd += [
        '-find', '/EFI/boot', '-name', 'bootx64.efi', '-exec', 'rm', '--',
        '-find', '/EFI/boot', '-name', 'grubx64.efi', '-exec', 'rm', '--',
        '-find', '/EFI/boot', '-name', 'mmx64.efi', '-exec', 'rm', '--',
        '-commit',
    ]
    _run(*cmd)


def inject_first_boot_unit(
    iso: Path, sqfs_in_iso: str, uboot_efi: Path, work: Path,
) -> Path:
    """Unpack the install squashfs, drop in a first-boot BLS-setup unit
    plus its helper script and a copy of u-boot.efi, and repack.

    u-boot.efi travels through the install at /usr/lib/u-boot/u-boot.efi
    so the first-boot unit (and the autoinstall late-commands, which
    also run in-target after the squashfs has been unpacked) can copy
    it onto the installed ESP.

    The whole unpack/modify/repack cycle runs under one fakeroot
    invocation so ownership survives round-tripping through a regular
    user filesystem. Compression matches the original (xz) to keep the
    resulting ISO close to the source in size; that does mean the
    rewrite takes several minutes.

    Returns the path to the modified squashfs.
    """
    extracted = work / 'orig.squashfs'
    modified = work / 'modified.squashfs'
    stage = work / 'sqfs-stage'
    aux = work / 'aux'
    aux.mkdir()
    script_src = aux / 'ubuntu-iso-to-uboot-bls-setup'
    script_src.write_text(FIRST_BOOT_SCRIPT)
    script_src.chmod(0o755)
    unit_src = aux / 'ubuntu-iso-to-uboot-bls-setup.service'
    unit_src.write_text(FIRST_BOOT_UNIT)

    tout.notice(f'=> Extracting {sqfs_in_iso}')
    _run(
        'xorriso', '-osirrox', 'on', '-indev', str(iso),
        '-extract', '/' + sqfs_in_iso.lstrip('/'), str(extracted),
    )

    tout.notice('=> Unpacking, injecting unit, repacking (xz, slow)')
    shell = f'''
set -e
unsquashfs -d '{stage}' '{extracted}'
install -D -m 755 -o root -g root '{script_src}' \\
    '{stage}/usr/local/sbin/ubuntu-iso-to-uboot-bls-setup'
install -D -m 644 -o root -g root '{unit_src}' \\
    '{stage}/etc/systemd/system/ubuntu-iso-to-uboot-bls-setup.service'
install -D -m 644 -o root -g root '{uboot_efi}' \\
    '{stage}/usr/lib/u-boot/u-boot.efi'
mkdir -p '{stage}/etc/systemd/system/multi-user.target.wants'
ln -sf ../ubuntu-iso-to-uboot-bls-setup.service \\
    '{stage}/etc/systemd/system/multi-user.target.wants/ubuntu-iso-to-uboot-bls-setup.service'
chown -h root:root \\
    '{stage}/etc/systemd/system/multi-user.target.wants/ubuntu-iso-to-uboot-bls-setup.service'
mksquashfs '{stage}' '{modified}' -noappend -comp xz -no-progress
'''
    _run('fakeroot', 'sh', '-c', shell)
    return modified


def hash_password(password: str) -> str:
    """Return a SHA-512 crypt hash for @password via openssl passwd -6.

    Subiquity's identity.password field wants a crypt(3) hash, not a plaintext
    password. Shelling out to openssl keeps the script's dependency list
    unchanged (Python's crypt module is gone in 3.13).
    """
    out = subprocess.run(
        ['openssl', 'passwd', '-6', '-stdin'],
        input=password, capture_output=True, text=True, check=True,
    )
    return out.stdout.strip()


def autoinstall_yaml(
    unattended: bool = False,
    hostname: str = 'ubuntu-uboot',
    username: str = 'ubuntu',
    password_hash: str = '',
) -> str:
    """Return an autoinstall snippet that seeds BLS entries on the
    installed ESP.

    The Ubuntu installer (ubiquity/subiquity) reads /autoinstall.yaml from the
    install media when autoinstall mode is invoked; the file is ignored in plain
    interactive installs, so shipping it by default is harmless. When
    autoinstall runs, the late-commands here write a BLS Type #1 entry to
    /boot/efi/loader/entries/<v>.conf for every kernel the installer has
    unpacked and copy the kernel and initrd alongside on the ESP, so U-Boot's
    bootmeth_bls finds the entry on the ESP partition before the EFI bootmeth on
    the same partition chain-loads U-Boot's BOOTX64.EFI fallback into a loop.

    The kernel-install path is bypassed because curtin's chroot has /boot/efi as
    a plain directory rather than the ESP mountpoint, so systemd-boot-efi's
    90-loaderentry.install plugin silently exits without writing entries.

    When @unattended is True, also emit identity and storage sections so
    subiquity can complete without user input - required for the
    test_distro_ubuntu_iso_install CI path.
    """
    head = '''\
#cloud-config
autoinstall:
  version: 1
'''
    unattended_block = ''
    if unattended:
        unattended_block = f'''\
  interactive-sections: []
  refresh-installer:
    update: no
  identity:
    hostname: {hostname}
    username: {username}
    password: '{password_hash}'
  storage:
    layout:
      name: direct
  ssh:
    install-server: true
'''
    body = (
        '  late-commands:\n'
        # Write BLS entries to the install target's ESP. The kernel
        # and initrd are copied alongside on the ESP so the entry can
        # reference them by ESP-relative paths. We place the entries
        # on the ESP rather than /boot/loader/entries on the rootfs
        # because U-Boot's bootflow iterator advances per-partition:
        # the ESP is the lower-numbered partition and EFI bootmeth
        # finds our BOOTX64.EFI fallback there before BLS reaches the
        # rootfs further down. Putting the entries on the ESP lets
        # BLS win on that partition before EFI has a turn. We also
        # write a /loader/entry.conf (singular) fallback matching the
        # format the live ISO already uses, so U-Boot picks the entry
        # whether its scan prefers the entries/ directory or the
        # singular file.
        '''    - |
      curtin in-target -- sh -c 'set -e;\
 ESP=/boot/efi;\
 mkdir -p "$ESP/loader/entries";\
 uuid=$(findmnt -no UUID /);\
 last_v="";\
 for k in /usr/lib/modules/*; do\
 v=$(basename "$k");\
 cp "/boot/vmlinuz-$v" "$ESP/vmlinuz-$v";\
 cp "/boot/initrd.img-$v" "$ESP/initrd.img-$v";\
 printf "title Ubuntu (%s)\\n\
linux /vmlinuz-%s\\n\
initrd /initrd.img-%s\\n\
options root=UUID=%s ro console=ttyS0,115200 console=tty0\\n"\
 "$v" "$v" "$v" "$uuid"\
 > "$ESP/loader/entries/$v.conf";\
 last_v="$v";\
 done;\
 if [ -n "$last_v" ]; then\
 cp "$ESP/loader/entries/$last_v.conf" "$ESP/loader/entry.conf";\
 fi'
'''
        # Plant u-boot.efi on the installed ESP in-target, overwriting
        # both the fallback BOOTX64.EFI and the shimx64.efi that the
        # 'ubuntu' NVRAM entry points at, so the disk boots U-Boot
        # whichever path firmware takes - no need to keep the ISO
        # attached after the install reboot.
        '''    - curtin in-target -- install -D -m 644\
 /usr/lib/u-boot/u-boot.efi /boot/efi/EFI/BOOT/BOOTX64.EFI
    - |
      curtin in-target -- sh -c '[ -f\
 /boot/efi/EFI/ubuntu/shimx64.efi ] && install -m 644\
 /usr/lib/u-boot/u-boot.efi\
 /boot/efi/EFI/ubuntu/shimx64.efi; true'
'''
    )
    return head + unattended_block + body


def main() -> None:
    p = argparse.ArgumentParser(
        description='Rewrite an Ubuntu live ISO to boot via U-Boot + BLS.',
    )
    p.add_argument('iso', type=Path, help='input Ubuntu live ISO')
    p.add_argument('-u', '--uboot', type=Path, required=True,
                   help='U-Boot EFI app (e.g. u-boot-app.efi)')
    p.add_argument('-o', '--out', type=Path, required=True,
                   help='output ISO path')
    p.add_argument('-k', '--kernel', default='casper/vmlinuz',
                   help='kernel path inside the input ISO')
    p.add_argument('-i', '--initrd', default='casper/initrd',
                   help='initrd path inside the input ISO')
    p.add_argument('-a', '--cmdline', default=None,
                   help='kernel command line written into loader/entry.conf '
                        '(default: inherit from the ISO\'s grub.cfg)')
    p.add_argument('-t', '--title', default=None,
                   help='BLS entry title (default: derived from volume label)')
    p.add_argument('-s', '--esp-size', type=int, default=None,
                   help='ESP size in MiB (default: 4 MiB)')
    p.add_argument('-N', '--no-target-bls', action='store_true',
                   help='do not ship an autoinstall snippet or modify '
                        'the install squashfs to wire kernel-install + '
                        'BLS into the installed system')
    p.add_argument('-I', '--install-squashfs',
                   default='casper/minimal.squashfs',
                   help='path inside the ISO of the install squashfs to '
                        'modify for interactive installs '
                        '(default: %(default)s; set to empty to skip)')
    p.add_argument('-A', '--autoinstall', action='store_true',
                   help='build for unattended autoinstall: append '
                        '"autoinstall" to the BLS cmdline and ship a complete '
                        'autoinstall.yaml so subiquity runs without user input')
    p.add_argument('--hostname', default='ubuntu-uboot',
                   help='hostname for the autoinstalled system '
                        '(default: %(default)s)')
    p.add_argument('--username', default='ubuntu',
                   help='username created by autoinstall '
                        '(default: %(default)s)')
    p.add_argument('--password', default='ubuntu',
                   help='plaintext password for the autoinstall user; '
                        'hashed with `openssl passwd -6` before being '
                        'written to autoinstall.yaml (default: %(default)s)')
    p.add_argument('-v', '--verbose', action='store_true',
                   help='show progress markers and subprocess output')
    args = p.parse_args()

    # Default verbosity is WARNING (silent); -v raises to INFO so
    # tout.notice()/tout.info() print and _run() stops capturing output.
    tout.init(tout.INFO if args.verbose else tout.WARNING)

    if not args.iso.is_file():
        tout.fatal(f'ISO not found: {args.iso}')
    if not args.uboot.is_file():
        tout.fatal(f'EFI app not found: {args.uboot}')
    if args.autoinstall and args.no_target_bls:
        tout.fatal('--autoinstall requires target-BLS wiring; drop -N')
    if args.autoinstall and not args.install_squashfs:
        tout.fatal(
            '--autoinstall needs --install-squashfs; the autoinstall '
            "late-commands copy u-boot.efi from /usr/lib/u-boot/u-boot.efi, "
            'which is only planted when the install squashfs is modified'
        )
    check_tools()
    if args.autoinstall and not shutil.which('openssl'):
        tout.fatal('openssl is required when --autoinstall is set')

    tout.notice(f'=> Reading boot config from {args.iso}')
    # Extract the volume label and ESP partition GUID from xorriso's report
    vol_id, esp_guid = parse_boot_report(args.iso)
    title = args.title or f'U-Boot BLS boot ({vol_id})'
    cmdline = args.cmdline
    if cmdline is None:
        cmdline = parse_grub_cmdline(args.iso, args.kernel)
    if args.autoinstall:
        # Force subiquity onto the serial console so the CI harness can watch
        # progress: drop the live-ISO 'quiet splash' (kernel would otherwise
        # silently swallow the reboot line), pin the console to ttyS0, and ask
        # systemd-journald to mirror its stream onto the console so subiquity's
        # own events land on serial too.
        tokens = [t for t in cmdline.split() if t not in ('quiet', 'splash')]
        for extra in ('console=ttyS0,115200',
                      'systemd.journald.forward_to_console=1',
                      'autoinstall'):
            if extra not in tokens:
                tokens.append(extra)
        cmdline = ' '.join(tokens)
    tout.notice(f'   Volume label: {vol_id}')
    tout.notice(f'   ESP GUID:     {esp_guid}')
    tout.notice(f'   Cmdline:      {cmdline}')

    with tempfile.TemporaryDirectory(prefix='iso2uboot.') as td:
        work = Path(td)

        esp_mib = args.esp_size or 4
        tout.notice(f'=> Building {esp_mib} MiB ESP')
        esp = work / 'esp.img'
        build_esp(esp, esp_mib, args.uboot)

        entry = work / 'entry.conf'
        entry.write_text(
            f'title {title}\n'
            f'linux /{args.kernel}\n'
            f'initrd /{args.initrd}\n'
            f'options {cmdline}\n'
        )

        file_maps = [(entry, '/loader/entry.conf')]
        if not args.no_target_bls:
            ai = work / 'autoinstall.yaml'
            if args.autoinstall:
                ai.write_text(autoinstall_yaml(
                    unattended=True,
                    hostname=args.hostname,
                    username=args.username,
                    password_hash=hash_password(args.password),
                ))
            else:
                ai.write_text(autoinstall_yaml())
            file_maps.append((ai, '/autoinstall.yaml'))
            if args.install_squashfs:
                modified_sqfs = inject_first_boot_unit(
                    args.iso, args.install_squashfs, args.uboot, work,
                )
                file_maps.append(
                    (modified_sqfs, '/' + args.install_squashfs.lstrip('/')),
                )

        tout.notice(f'=> Repacking to {args.out}')
        repack_iso(args.iso, args.out, esp, esp_guid, file_maps)

    size_mib = args.out.stat().st_size / MIB
    tout.notice(f'=> Done: {args.out} ({size_mib:.1f} MiB)')


if __name__ == '__main__':
    main()
