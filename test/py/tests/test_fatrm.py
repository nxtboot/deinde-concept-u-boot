# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the fatrm command
"""

import pytest
from tests.fs_helper import FsHelper

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatrm_base(ubman):
    """Check that fatrm deletes a file so that it no longer appears

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatrm') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('mw.b 1000000 61 20')
        ubman.run_command('fatwrite host 0 1000000 doomed.txt 20')
        ubman.run_command('fatwrite host 0 1000000 keeper.txt 20')
        assert '2 file(s), 0 dir(s)' in ubman.run_command('fatls host 0')

        ubman.run_command('fatrm host 0 doomed.txt')
        assert '0' == ubman.run_command('echo $?')

        output = ubman.run_command('fatls host 0')
        assert 'doomed.txt' not in output
        assert '32   keeper.txt' in output
        assert '1 file(s), 0 dir(s)' in output

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatrm_dir(ubman):
    """Check that fatrm removes an empty directory but not one with files in

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatrm_dir') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('fatmkdir host 0 docs')
        ubman.run_command('mw.b 1000000 61 20')
        ubman.run_command('fatwrite host 0 1000000 docs/note.txt 20')

        # A directory with a file in it cannot be removed
        assert 'Error: directory is not empty' in ubman.run_command(
            'fatrm host 0 docs')
        assert '1' == ubman.run_command('echo $?')

        # Once it is empty it can
        ubman.run_command('fatrm host 0 docs/note.txt')
        ubman.run_command('fatrm host 0 docs')
        assert '0' == ubman.run_command('echo $?')
        assert '0 file(s), 0 dir(s)' in ubman.run_command('fatls host 0')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fat')
@pytest.mark.buildconfigspec('fat_write')
def test_fatrm_missing(ubman):
    """Check that fatrm fails when the file is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'vfat', 1, 'test_fatrm_missing') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        assert 'missing.txt: doesn\'t exist' in ubman.run_command(
            'fatrm host 0 missing.txt')
        assert '1' == ubman.run_command('echo $?')
