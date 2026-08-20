# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the fatmkdir command
"""

import pytest
from tests.fs_helper import FsHelper

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatmkdir_base(ubman):
    """Check that fatmkdir creates a directory which can then be written to

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatmkdir') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('fatmkdir host 0 docs')
        assert '0' == ubman.run_command('echo $?')

        # The new directory shows up in the root, with a trailing slash
        output = ubman.run_command('fatls host 0')
        assert 'docs/' in output
        assert '0 file(s), 1 dir(s)' in output

        # A new directory contains only the . and .. entries
        output = ubman.run_command('fatls host 0 docs')
        assert './' in output
        assert '../' in output
        assert '0 file(s), 2 dir(s)' in output

        # Files can be written into it
        ubman.run_command('mw.b 1000000 61 20')
        assert '32 bytes written' in ubman.run_command(
            'fatwrite host 0 1000000 docs/note.txt 20')
        assert '32   note.txt' in ubman.run_command('fatls host 0 docs')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatmkdir_exists(ubman):
    """Check that fatmkdir refuses to create a directory which is already there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatmkdir_exists') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('fatmkdir host 0 docs')
        output = ubman.run_command('fatmkdir host 0 docs')
        assert 'docs: already exists' in output
        assert 'Unable to create a directory "docs"' in output
        assert '1' == ubman.run_command('echo $?')
