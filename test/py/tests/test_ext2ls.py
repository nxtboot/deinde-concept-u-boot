# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the ext2ls command
"""

import os
import pytest
from tests.fs_helper import FsHelper

def make_image(fsh):
    """Create an ext4 image holding a file and a directory

    Args:
        fsh (FsHelper): Helper to create the image with
    """
    with open(f'{fsh.srcdir}/hello.txt', 'w', encoding='ascii') as outf:
        outf.write('Hello, world!\n')
    os.mkdir(f'{fsh.srcdir}/docs')
    with open(f'{fsh.srcdir}/docs/note.txt', 'w', encoding='ascii') as outf:
        outf.write('note\n')
    fsh.mk_fs()

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
@pytest.mark.buildconfigspec('fs_ext4l')
def test_ext2ls_base(ubman):
    """Check that ext2ls lists a directory with sizes and trailing slashes

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2ls') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # Entries come out in directory order, so check for each in turn
        out = ubman.run_command('ext2ls host 0')
        assert '0' == ubman.run_command('echo $?')
        assert '      14   hello.txt' in out
        assert '            docs/' in out
        assert '            lost+found/' in out

        # A directory can be listed on its own
        out = ubman.run_command('ext2ls host 0 /docs')
        assert '0' == ubman.run_command('echo $?')
        assert '       5   note.txt' in out
        assert '            ./' in out
        assert 'hello.txt' not in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
@pytest.mark.buildconfigspec('fs_ext4l')
def test_ext2ls_missing(ubman):
    """Check that ext2ls fails silently on anything which is not a directory

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2ls_missing') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        assert '' == ubman.run_command('ext2ls host 0 /nodir')
        assert '1' == ubman.run_command('echo $?')

        # A file is not a directory, so this fails in the same way
        assert '' == ubman.run_command('ext2ls host 0 hello.txt')
        assert '1' == ubman.run_command('echo $?')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_ext2')
@pytest.mark.buildconfigspec('fs_ext4l')
def test_ext2ls_baddev(ubman):
    """Check that ext2ls complains about a device which is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_ext2ls_baddev') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('ext2ls host 1')
        assert '1' == ubman.run_command('echo $?')
        assert '** Bad device specification host 1 **' in out
        assert "Couldn't find partition host 1" in out
