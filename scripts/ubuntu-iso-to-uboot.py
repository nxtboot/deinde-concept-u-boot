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
import sys
import tempfile
from pathlib import Path

# Add the tools directory to the path for u_boot_pylib
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'tools'))

# pylint: disable=wrong-import-position,import-error
from u_boot_pylib import command
from u_boot_pylib import tout

REQUIRED_TOOLS = ('xorriso', 'mcopy', 'mmd', 'mkfs.vfat')
MIB = 1024 * 1024


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
    entry_conf: Path,
) -> None:
    """Stream the input ISO to a new ISO with the ESP and BLS entry replaced.

    -boot_image any replay preserves every other boot record (BIOS El Torito,
    grub2 MBR, GPT layout); only the bytes behind partition 2 are rewritten,
    plus /loader/entry.conf is added to the ISO 9660 tree, and the shim,
    GRUB and MokManager copies under /EFI/boot/ are removed since U-Boot
    supplies the UEFI boot path via the appended ESP. The BIOS El Torito
    path still uses /boot/grub/ so legacy boot continues to work.

    -find is tolerant of missing files: if a distribution does not ship
    one of these binaries, the call is a no-op.

    xorriso refuses to write to an existing non-empty file when -indev
    and -outdev differ (it would treat the outdev as a session to
    extend), so unlink any stale output first.
    """
    if out_iso.exists():
        out_iso.unlink()
    _run(
        'xorriso',
        '-indev', str(in_iso),
        '-outdev', str(out_iso),
        '-boot_image', 'any', 'replay',
        '-append_partition', '2', esp_guid, str(esp_img),
        '-map', str(entry_conf), '/loader/entry.conf',
        '-find', '/EFI/boot', '-name', 'bootx64.efi', '-exec', 'rm', '--',
        '-find', '/EFI/boot', '-name', 'grubx64.efi', '-exec', 'rm', '--',
        '-find', '/EFI/boot', '-name', 'mmx64.efi', '-exec', 'rm', '--',
        '-commit',
    )


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
    check_tools()

    tout.notice(f'=> Reading boot config from {args.iso}')
    # Extract the volume label and ESP partition GUID from xorriso's report
    vol_id, esp_guid = parse_boot_report(args.iso)
    title = args.title or f'U-Boot BLS boot ({vol_id})'
    cmdline = args.cmdline
    if cmdline is None:
        cmdline = parse_grub_cmdline(args.iso, args.kernel)
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

        tout.notice(f'=> Repacking to {args.out}')
        repack_iso(args.iso, args.out, esp, esp_guid, entry)

    size_mib = args.out.stat().st_size / MIB
    tout.notice(f'=> Done: {args.out} ({size_mib:.1f} MiB)')


if __name__ == '__main__':
    main()
