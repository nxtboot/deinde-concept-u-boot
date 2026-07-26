# SPDX-License-Identifier: GPL-2.0
"""
Unit-test runner

Provides a test_ut() function which is used by conftest.py to run each unit
test one at a time, as well setting up some files needed by the tests.

# Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
"""
import collections
import gzip
import hashlib
import os
import os.path
import re
import pytest

import utils
# pylint: disable=E0611
from fs_helper import DiskHelper, FsHelper
from test_android import test_abootimg
from img.vbe import setup_vbe_image
from img.common import mkdir_cond, copy_partition, setup_extlinux_image
from img.fedora import setup_fedora_image
from img.ubuntu import setup_ubuntu_image
from img.armbian import setup_bootmenu_image
from img.bls import setup_bls_image
from img.chromeos import setup_cros_image
from img.android import setup_android_image
from img.efi import setup_efi_image
from img.cedit import setup_cedit_file
from img.localboot import setup_localboot_image
from img.rauc import setup_rauc_image


@pytest.mark.buildconfigspec('ut_dm')
def test_ut_dm_init(ubman):
    """Initialize data for ut dm tests."""

    # This is used by flash-stick@0 in test.py
    fn = ubman.config.persistent_data_dir + '/testflash.bin'
    if not os.path.exists(fn):
        data = b'this is a test'
        data += b'\x00' * ((4 * 1024 * 1024) - len(data))
        with open(fn, 'wb') as fh:
            fh.write(data)

    fn = ubman.config.persistent_data_dir + '/spi.bin'
    if not os.path.exists(fn):
        data = b'\x00' * (2 * 1024 * 1024)
        with open(fn, 'wb') as fh:
            fh.write(data)

    # Create a file with a single partition (used by /scsi in test.dts) */
    fn = ubman.config.persistent_data_dir + '/scsi.img'
    if not os.path.exists(fn):
        data = b'\x00' * (2 * 1024 * 1024)
        with open(fn, 'wb') as fh:
            fh.write(data)
        utils.run_and_log(
            ubman, f'sfdisk {fn}', stdin=b'type=83')

    # These two are used by test/dm/host.c
    FsHelper(ubman.config, 'ext2', 2, '2MB').mk_fs()
    FsHelper(ubman.config, 'fat32', 1, '1MB').mk_fs()

    # This is used by test/cmd/mbr.c
    mmc_dev = 6
    fn = os.path.join(ubman.config.persistent_data_dir, f'mmc{mmc_dev}.img')
    data = b'\x00' * (12 * 1024 * 1024)
    with open(fn, 'wb') as fh:
        fh.write(data)

    mmc_dev = 9
    fn = os.path.join(ubman.config.source_dir, f'mmc{mmc_dev}.img')
    data = b'\x00' * (32 * 1024 * 1024)
    with open(fn, 'wb') as fh:
        fh.write(data)


@pytest.mark.buildconfigspec('cmd_bootflow')
@pytest.mark.buildconfigspec('sandbox')
def test_ut_dm_init_bootstd(u_boot_config, u_boot_log):
    """Initialise data for bootflow tests

    Args:
        u_boot_config (ArbitraryAttributeContainer): Configuration
        u_boot_log (multiplexed_log.Logfile): Log to write to
    """
    setup_fedora_image(u_boot_config, u_boot_log, 1, 'mmc')
    setup_bls_image(u_boot_config, u_boot_log, 15, 'mmc')
    setup_bootmenu_image(u_boot_config, u_boot_log)
    setup_cedit_file(u_boot_config, u_boot_log)
    setup_cros_image(u_boot_config, u_boot_log)
    setup_android_image(u_boot_config, u_boot_log)
    setup_efi_image(u_boot_config)
    setup_rauc_image(u_boot_config, u_boot_log)
    setup_ubuntu_image(u_boot_config, u_boot_log, 3, 'flash', '25.04')
    setup_localboot_image(u_boot_config, u_boot_log)
    setup_vbe_image(u_boot_config, u_boot_log)

    # Generate TKey emulator disk key for LUKS encryption
    # The emulator generates pubkey as 0x50 + (i & 0xf) for i in range(32)
    # Disk key = SHA256(hex_string_of_pubkey), matching tkey_derive_disk_key()
    # Allow override via external key file for testing with real keys
    override_keyfile = os.path.join(u_boot_config.source_dir, 'override.bin')
    if os.path.exists(override_keyfile):
        keyfile = override_keyfile
        u_boot_log.action(f'Using override TKey key: {keyfile}')
    else:
        pubkey = bytes([0x50 + (i & 0xf) for i in range(32)])
        disk_key = hashlib.sha256(pubkey.hex().encode()).digest()
        keyfile = os.path.join(u_boot_config.persistent_data_dir, 'tkey_emul.key')
        with open(keyfile, 'wb') as f:
            f.write(disk_key)
        u_boot_log.action(f'Generated TKey emulator disk key: {keyfile}')

    setup_ubuntu_image(u_boot_config, u_boot_log, 11, 'mmc', use_fde=1)
    setup_ubuntu_image(u_boot_config, u_boot_log, 12, 'mmc', use_fde=2,
                       luks_kdf='argon2id')
    setup_ubuntu_image(u_boot_config, u_boot_log, 13, 'mmc', use_fde=2,
                       luks_kdf='argon2id', encrypt_keyfile=keyfile)

    # Create mmc14 with a known master key for pre_derived unlock testing
    # For LUKS2 with aes-xts-plain64, we need a 64-byte (512-bit) master key
    master_key = bytes([0x20 + (i & 0x3f) for i in range(64)])
    master_keyfile = os.path.join(u_boot_config.persistent_data_dir,
                                  'luks_master.key')
    with open(master_keyfile, 'wb') as f:
        f.write(master_key)
    u_boot_log.action(f'Generated LUKS master key: {master_keyfile}')
    setup_ubuntu_image(u_boot_config, u_boot_log, 14, 'mmc', use_fde=2,
                       luks_kdf='argon2id', encrypt_keyfile=keyfile,
                       master_keyfile=master_keyfile)

@pytest.fixture(name="ut_ubman")
def ut_ubman_fixture(ubman, ut_subtest):
    """Fixture to restart the sandbox after known problematic tests.

    Args:
        ubman (ConsoleBase): U-Boot console
        ut_subtest (str): test to be executed via command ut, e.g 'foo bar' to
            execute command 'ut foo bar'
    """

    yield ubman

    if ut_subtest in ("bootstd bootflow_cmd_boot", "bootstd bootflow_scan_boot",
                      "bootstd bootflow_scan_extlinux"):
        ubman.restart_uboot()


def test_ut(ut_ubman, ut_subtest):
    """Execute a "ut" subtest.

    The subtests are collected in function generate_ut_subtest() from linker
    generated lists by applying a regular expression to the lines of file
    u-boot.sym. The list entries are created using the C macro UNIT_TEST().

    Strict naming conventions have to be followed to match the regular
    expression. Use UNIT_TEST(foo_test_bar, _flags, foo_test) for a test bar in
    test suite foo that can be executed via command 'ut foo bar' and is
    implemented in C function foo_test_bar().

    Args:
        ut_ubman (ConsoleBase): U-Boot console
        ut_subtest (str): test to be executed via command ut, e.g 'foo bar' to
            execute command 'ut foo bar'
    """

    flags = '-L ' if ut_ubman.config.leak_check else ''

    if ut_subtest == 'hush hush_test_simple_dollar':
        # ut hush hush_test_simple_dollar prints "Unknown command" on purpose.
        with ut_ubman.disable_check('unknown_command'):
            output = ut_ubman.run_command(f'ut {flags}' + ut_subtest)
        assert 'Unknown command \'quux\' - try \'help\'' in output
    else:
        output = ut_ubman.run_command(f'ut {flags}' + ut_subtest)
    assert output.endswith('failures: 0')
    lastline = output.splitlines()[-1]
    if "skipped: 0," not in lastline:
        match = re.search(r'skipped:\s*(\d+),', lastline)
        if match:
            count = match.group(1)
            pytest.skip(f'Test {ut_subtest} has {count} skipped sub-test(s).')
