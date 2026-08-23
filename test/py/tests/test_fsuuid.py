# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the fsuuid command
"""

import uuid
import pytest
from tests.fs_helper import FsHelper

# Offset of the ext2/3/4 superblock in the filesystem, and of s_uuid within it
EXT_SB_OFFSET = 1024
EXT_SB_UUID = 0x68

# Offset of the FAT32 volume ID within the boot sector
FAT32_VOLID = 0x43

def make_image(fsh):
    """Create a filesystem image holding a small text file

    Args:
        fsh (FsHelper): Helper to create the image with
    """
    with open(f'{fsh.srcdir}/hello.txt', 'w', encoding='ascii') as outf:
        outf.write('Hello, world!\n')
    fsh.mk_fs()

def read_ext_uuid(fname):
    """Read the UUID recorded in an ext2/3/4 superblock

    Args:
        fname (str): Filename of the filesystem image

    Returns:
        str: UUID in the usual 8-4-4-4-12 form
    """
    with open(fname, 'rb') as inf:
        inf.seek(EXT_SB_OFFSET + EXT_SB_UUID)
        return str(uuid.UUID(bytes=inf.read(16)))

def read_fat_volid(fname):
    """Read the volume ID recorded in a FAT32 boot sector

    U-Boot shows this as two groups of four hex digits, most-significant
    first, since a FAT filesystem has no room for a full UUID.

    Args:
        fname (str): Filename of the filesystem image

    Returns:
        str: Volume ID in the form U-Boot prints it
    """
    with open(fname, 'rb') as inf:
        inf.seek(FAT32_VOLID)
        vid = inf.read(4)
    return f'{vid[3]:02X}{vid[2]:02X}-{vid[1]:02X}{vid[0]:02X}'

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_base(ubman):
    """Check that fsuuid prints the UUID held in the filesystem

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_fsuuid') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('fsuuid host 0')
        assert '0' == ubman.run_command('echo $?')
        assert read_ext_uuid(fsh.fs_img) == out

        # The partition defaults to 0, so naming it explicitly is the same
        assert out == ubman.run_command('fsuuid host 0:0')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_var(ubman):
    """Check that fsuuid can put the UUID in an environment variable

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_fsuuid_var') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        # With a variable named, nothing is printed
        assert '' == ubman.run_command('fsuuid host 0 test_uuid')
        assert '0' == ubman.run_command('echo $?')
        assert read_ext_uuid(fsh.fs_img) == ubman.run_command(
            'echo $test_uuid')

        # A failed lookup leaves the variable alone
        ubman.run_command('setenv test_uuid sentinel')
        ubman.run_command('fsuuid host 9 test_uuid')
        assert '1' == ubman.run_command('echo $?')
        assert 'sentinel' == ubman.run_command('echo $test_uuid')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_fat(ubman):
    """Check that fsuuid shows the shorter volume ID of a FAT filesystem

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'fat32', 2, 'test_fsuuid_fat') as fsh:
        fsh.mk_fs()
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('fsuuid host 0')
        assert '0' == ubman.run_command('echo $?')
        assert read_fat_volid(fsh.fs_img) == out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_missing(ubman):
    """Check that fsuuid reports a device which is not there

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_fsuuid_missing') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('fsuuid host 9')
        assert '1' == ubman.run_command('echo $?')
        assert '** Bad device specification host 9 **' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_option(ubman):
    """Check that fsuuid refuses an option, since it has none

    Args:
        ubman -- U-Boot console
    """
    with FsHelper(ubman.config, 'ext4', 2, 'test_fsuuid_option') as fsh:
        make_image(fsh)
        ubman.run_command(f'host bind 0 {fsh.fs_img}')

        out = ubman.run_command('fsuuid -x host 0')
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out

        # An option after the interface is a variable name, as before. The
        # UUID goes into a variable called -x, which the shell cannot expand,
        # so the proof is that nothing was printed and the command succeeded
        assert '' == ubman.run_command('fsuuid host 0 -x')
        assert '0' == ubman.run_command('echo $?')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_fs_uuid')
def test_fsuuid_args(ubman):
    """Check that fsuuid rejects the wrong number of arguments

    Args:
        ubman -- U-Boot console
    """
    for cmd in ['fsuuid', 'fsuuid host', 'fsuuid host 0 var extra']:
        out = ubman.run_command(cmd)
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out
