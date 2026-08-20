# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the fatwrite command
"""

import pytest
from tests.fs_helper import FsHelper

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatwrite_base(ubman):
    """Check that fatwrite writes memory to a file which reads back the same

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatwrite') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('mw.b 1000000 5a 40')
        assert '64 bytes written' in ubman.run_command(
            'fatwrite host 0 1000000 data.bin 40')

        # The file must show up with the size just written
        assert '64   data.bin' in ubman.run_command('fatls host 0')

        # Read it back to a different address and compare
        ubman.run_command('mw.b 2000000 0 40')
        ubman.run_command('fatload host 0 2000000 data.bin')
        assert 'Total of 64 byte(s) were the same' in ubman.run_command(
            'cmp.b 1000000 2000000 40')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatwrite_offset(ubman):
    """Check that fatwrite can update part of an existing file

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatwrite_offset') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('mw.b 1000000 5a 40')
        ubman.run_command('fatwrite host 0 1000000 data.bin 40')

        # Overwrite the last 0x10 bytes with a different value
        ubman.run_command('mw.b 1000000 a5 10')
        assert '16 bytes written' in ubman.run_command(
            'fatwrite host 0 1000000 data.bin 10 30')

        # The file must still be 0x40 bytes long
        assert '64   data.bin' in ubman.run_command('fatls host 0')

        ubman.run_command('fatload host 0 2000000 data.bin')
        assert 'Total of 16 byte(s) were the same' in ubman.run_command(
            'cmp.b 1000000 2000030 10')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatwrite_no_dir(ubman):
    """Check that fatwrite fails when the parent directory is missing

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatwrite_no_dir') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('mw.b 1000000 5a 40')
        output = ubman.run_command('fatwrite host 0 1000000 nodir/data.bin 40')
        assert 'nodir: doesn\'t exist' in output
        assert 'Unable to write file nodir/data.bin' in output
        assert '1' == ubman.run_command('echo $?')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatwrite_no_space(ubman):
    """Check that fatwrite fails when the file does not fit

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatwrite_no_space') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # The filesystem is 1MB, so 0x200000 bytes cannot fit
        output = ubman.run_command('fatwrite host 0 1000000 big.bin 200000')
        assert 'Error: no space left' in output
        assert 'Unable to write file big.bin' in output
        assert '1' == ubman.run_command('echo $?')
