# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the btrsubvol command
"""

import re
import pytest
from tests.fs_helper import FsHelper

# Smallest BTRFS image mkfs.btrfs will make with mixed block groups, in MB
BTRFS_MB = 16

# The top-level filesystem tree, which every BTRFS filesystem has
TOP_LEVEL = re.compile(r'^ID 5 gen \d+ path /$')

def make_image(fsh):
    """Create a filesystem image holding a small text file

    Args:
        fsh (FsHelper): Helper to create the image with
    """
    with open(f'{fsh.srcdir}/hello.txt', 'w', encoding='ascii') as outf:
        outf.write('Hello, world!\n')
    fsh.mk_fs()

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_btrfs')
def test_btrsubvol_base(ubman):
    """Check that btrsubvol lists the top-level subvolume

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'btrfs', BTRFS_MB, 'test_btrsubvol') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # A freshly created filesystem has only the top-level tree
        out = ubman.run_command('btrsubvol host 0')
        assert '0' == ubman.run_command('echo $?')
        assert TOP_LEVEL.match(out)

        # The partition defaults to 0, so naming it explicitly is the same
        assert TOP_LEVEL.match(ubman.run_command('btrsubvol host 0:0'))

        # Check that this really is a filesystem U-Boot can read
        out = ubman.run_command('load host 0 1000000 hello.txt')
        assert '14 bytes read' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_btrfs')
def test_btrsubvol_other_fs(ubman):
    """Check that btrsubvol refuses a filesystem which is not BTRFS

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_btrsubvol_ext') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # Nothing is printed, so the return value is the only sign
        assert '' == ubman.run_command('btrsubvol host 0')
        assert '1' == ubman.run_command('echo $?')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_btrfs')
def test_btrsubvol_missing(ubman):
    """Check that btrsubvol reports a device which is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'btrfs', BTRFS_MB,
                  'test_btrsubvol_missing') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('btrsubvol host 9')
        assert '1' == ubman.run_command('echo $?')
        assert '** Bad device specification host 9 **' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_btrfs')
def test_btrsubvol_args(ubman):
    """Check that btrsubvol rejects the wrong number of arguments

    Args:
        ubman -- U-Boot console
    """
    for cmd in ['btrsubvol', 'btrsubvol host', 'btrsubvol host 0 extra']:
        out = ubman.run_command(cmd)
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out
