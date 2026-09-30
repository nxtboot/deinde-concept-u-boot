# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the ext4size command
"""

import os
import pytest
from tests.fs_helper import FsHelper

def make_image(fsh):
    """Create an ext4 image holding a file and a directory

    Args:
        fsh (FsHelper): Helper to create the image with
    """
    # 14 bytes including the newline
    with open(f'{fsh.srcdir}/hello.txt', 'w', encoding='ascii') as outf:
        outf.write('Hello, world!\n')
    os.mkdir(f'{fsh.srcdir}/docs')

    # 5 bytes including the newline
    with open(f'{fsh.srcdir}/docs/note.txt', 'w', encoding='ascii') as outf:
        outf.write('note\n')
    fsh.mk_fs()

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext4')
def test_ext4size_base(ubman):
    """Check that ext4size sets filesize and prints nothing

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext4size') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # The size is set in hex, so 14 bytes is 0xe
        assert '' == ubman.run_command('ext4size host 0 hello.txt')
        assert '0' == ubman.run_command('echo $?')
        assert 'e' == ubman.run_command('echo $filesize')

        # A file in a subdirectory works the same way
        assert '' == ubman.run_command('ext4size host 0 /docs/note.txt')
        assert '0' == ubman.run_command('echo $?')
        assert '5' == ubman.run_command('echo $filesize')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext4')
def test_ext4size_missing(ubman):
    """Check that ext4size leaves filesize alone when the file is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext4size_missing') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('setenv filesize sentinel')
        assert '' == ubman.run_command('ext4size host 0 missing.txt')
        assert '1' == ubman.run_command('echo $?')
        assert 'sentinel' == ubman.run_command('echo $filesize')

        # A path through a directory which is not there fails the same way
        assert '' == ubman.run_command('ext4size host 0 /nodir/note.txt')
        assert '1' == ubman.run_command('echo $?')
        assert 'sentinel' == ubman.run_command('echo $filesize')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext4')
def test_ext4size_dir(ubman):
    """Check that ext4size reports a size for a directory

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext4size_dir') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # The filesystem layer does not check that the path is a file, so a
        # directory reports the size of its inode rather than failing
        assert '' == ubman.run_command('ext4size host 0 /docs')
        assert '0' == ubman.run_command('echo $?')
        assert '1000' == ubman.run_command('echo $filesize')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext4')
def test_ext4size_baddev(ubman):
    """Check that ext4size complains about a device which is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext4size_baddev') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('ext4size host 1 hello.txt')
        assert '1' == ubman.run_command('echo $?')
        assert '** Bad device specification host 1 **' in out
        assert "Couldn't find partition host 1" in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext4')
def test_ext4size_usage(ubman):
    """Check that ext4size wants all three of its arguments

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext4size_usage') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('ext4size host 0')
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out
        assert 'ext4size <interface> <dev[:part]> <filename>' in out
