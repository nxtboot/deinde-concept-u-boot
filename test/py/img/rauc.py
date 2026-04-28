# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 PHYTEC Messtechnik GmbH

"""Create RAUC test disk images"""

import os

import utils
import fs_helper


def setup_rauc_image(config, log):
    """Create a 40MB disk image with an A/B RAUC system on it

    Args:
        config (ArbitraryAttributeContainer): Configuration
        log (multiplexed_log.Logfile): Log to write to
    """
    mmc_dev = 16
    fname = os.path.join(config.source_dir, f'mmc{mmc_dev}.img')
    mnt = config.persistent_data_dir

    spec = 'type=c, size=8M, start=1M, bootable\n' \
           'type=83, size=10M\n' \
           'type=c, size=8M, bootable\n' \
           'type=83, size=10M'

    utils.run_and_log_no_ubman(log, f'qemu-img create {fname} 40M')
    utils.run_and_log_no_ubman(
        log, ['sh', '-c', f'printf "{spec}" | sfdisk {fname}'])

    # Generate boot script
    script = '# dummy boot script'
    bootdir = os.path.join(mnt, 'boot')
    utils.run_and_log_no_ubman(log, f'mkdir -p {bootdir}')
    cmd_fname = os.path.join(bootdir, 'boot.cmd')
    scr_fname = os.path.join(bootdir, 'boot.scr')
    with open(cmd_fname, 'w', encoding='ascii') as outf:
        print(script, file=outf)

    mkimage = os.path.join(config.build_dir, 'tools/mkimage')
    utils.run_and_log_no_ubman(
        log,
        f'{mkimage} -C none -A arm -T script -d {cmd_fname} {scr_fname}')
    utils.run_and_log_no_ubman(log, f'rm -f {cmd_fname}')

    # Generate empty rootfs
    rootdir = os.path.join(mnt, 'root')
    utils.run_and_log_no_ubman(log, f'mkdir -p {rootdir}')

    # Create boot filesystem image with boot script and copy to disk image
    fsfile = fs_helper.mk_fs(config, 'fat32', 0x800000, 'rauc_boot', bootdir)
    utils.run_and_log_no_ubman(
        log, f'dd if={fsfile} of={fname} bs=1M seek=1 conv=notrunc')
    utils.run_and_log_no_ubman(
        log, f'dd if={fsfile} of={fname} bs=1M seek=19 conv=notrunc')
    utils.run_and_log_no_ubman(log, f'rm -f {scr_fname}')

    # Create empty root filesystem image and copy to disk image
    fsfile = fs_helper.mk_fs(config, 'ext4', 0xa00000, 'rauc_root', rootdir)
    utils.run_and_log_no_ubman(
        log, f'dd if={fsfile} of={fname} bs=1M seek=9 conv=notrunc')
    utils.run_and_log_no_ubman(
        log, f'dd if={fsfile} of={fname} bs=1M seek=27 conv=notrunc')
    utils.run_and_log_no_ubman(log, f'rm -f {fsfile}')
