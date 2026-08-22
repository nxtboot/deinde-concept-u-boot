# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the ext2load command
"""

import pytest
from tests.fs_helper import FsHelper

# Contents of hello.txt, 14 bytes including the newline
HELLO = 'Hello, world!\n'

def make_image(fsh):
    """Create an ext4 image holding a small text file

    Args:
        fsh (FsHelper): Helper to create the image with
    """
    with open(f'{fsh.srcdir}/hello.txt', 'w', encoding='ascii') as outf:
        outf.write(HELLO)
    fsh.mk_fs()

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
def test_ext2load_base(ubman):
    """Check that ext2load reads a file and reports what it read

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2load') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('ext2load host 0 1000000 hello.txt')
        assert '0' == ubman.run_command('echo $?')
        assert '14 bytes read' in out

        # Both are set in hex, so 14 bytes is 0xe
        assert 'e' == ubman.run_command('echo $filesize')
        assert '1000000' == ubman.run_command('echo $fileaddr')

        assert 'Hello, world!' in ubman.run_command('md.b 1000000 e')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
def test_ext2load_part(ubman):
    """Check that ext2load can read part of a file

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2load_part') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # Read 5 bytes from offset 7, which is 'world'
        out = ubman.run_command('ext2load host 0 1000000 hello.txt 5 7')
        assert '0' == ubman.run_command('echo $?')
        assert '5 bytes read' in out
        assert '5' == ubman.run_command('echo $filesize')
        assert 'world' in ubman.run_command('md.b 1000000 5')

        # Asking for more than there is stops at the end of the file
        out = ubman.run_command('ext2load host 0 1000000 hello.txt 100')
        assert '0' == ubman.run_command('echo $?')
        assert '14 bytes read' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
def test_ext2load_default(ubman):
    """Check that ext2load falls back on the bootfile and loadaddr variables

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2load_default') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # These are used by other tests, so put them back afterwards
        old_bootfile = ubman.run_command('echo $bootfile')
        old_loadaddr = ubman.run_command('echo $loadaddr')
        try:
            ubman.run_command('setenv bootfile')
            out = ubman.run_command('ext2load host 0 1000000')
            assert '1' == ubman.run_command('echo $?')
            assert '** No boot file defined **' in out

            ubman.run_command('setenv bootfile hello.txt')
            ubman.run_command('setenv loadaddr 2000000')
            out = ubman.run_command('ext2load host 0')
            assert '0' == ubman.run_command('echo $?')
            assert '14 bytes read' in out
            assert '2000000' == ubman.run_command('echo $fileaddr')
        finally:
            ubman.run_command(f'setenv bootfile {old_bootfile}')
            ubman.run_command(f'setenv loadaddr {old_loadaddr}')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
def test_ext2load_missing(ubman):
    """Check that ext2load fails without disturbing filesize and fileaddr

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2load_missing') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        ubman.run_command('setenv filesize sentinel')
        ubman.run_command('setenv fileaddr sentinel')
        out = ubman.run_command('ext2load host 0 1000000 missing.txt')
        assert '1' == ubman.run_command('echo $?')
        assert "Failed to load 'missing.txt'" in out
        assert 'sentinel' == ubman.run_command('echo $filesize')
        assert 'sentinel' == ubman.run_command('echo $fileaddr')
