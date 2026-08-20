# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the fatsize command
"""

import pytest
from tests.fs_helper import FsHelper

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatsize_base(ubman):
    """Check that fatsize reports a file's size in the filesize variable

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatsize') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # Create a file of 0x2a bytes to ask about
        ubman.run_command('mw.b 1000000 61 2a')
        assert '42 bytes written' in ubman.run_command(
            'fatwrite host 0 1000000 sized.txt 2a')

        # filesize is set in hex, so 0x2a
        ubman.run_command('setenv filesize sentinel')
        ubman.run_command('fatsize host 0 sized.txt')
        assert '0' == ubman.run_command('echo $?')
        assert '2a' == ubman.run_command('echo $filesize')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatsize_missing(ubman):
    """Check that fatsize fails and leaves filesize alone for a missing file

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatsize_missing') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('setenv filesize sentinel')
        ubman.run_command('fatsize host 0 missing.txt')
        assert '1' == ubman.run_command('echo $?')
        assert 'sentinel' == ubman.run_command('echo $filesize')
