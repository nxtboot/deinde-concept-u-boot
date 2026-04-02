# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# Test VFS operations on real filesystem images

"""
Test VFS mount, ls, stat, load, save, cat, mkdir, rm, mv on ext4 and
FAT images. Python creates the filesystem image; C tests mount it via
VFS and exercise the operations.
"""

import os

import pytest

from tests.fs_helper import FsHelper


# Tests shared by all filesystems. Each entry 'foo' runs the C test
# fs_test_vfs_foo. Filesystem-specific tests (e.g. ext4-only 'ln') are
# listed separately and use the fs_test_vfs_<fstype>_<name> naming.
VFS_TESTS = [
    'cat', 'cd', 'df', 'load', 'ls', 'mkdir', 'mv', 'nested', 'rm', 'save',
    'size', 'stat',
]

EXT4_TESTS = ['ln']


def _define_test(name, prefix='vfs'):
    """Create a test method that runs fs_test_<prefix>_<name>."""

    def test(self, ubman, fs_image):
        ubman.run_ut('fs', f'fs_test_{prefix}_{name}',
                     fs_image=fs_image)

    test.__name__ = f'test_{name}'
    test.__doc__ = f'Test {name} on a VFS-mounted filesystem.'
    return test


def _populate_srcdir(srcdir):
    """Create the standard test files in a source directory."""
    with open(os.path.join(srcdir, 'testfile.txt'), 'w') as f:
        f.write('hello world\n')
    subdir = os.path.join(srcdir, 'subdir')
    os.mkdir(subdir)
    with open(os.path.join(subdir, 'nested.txt'), 'w') as f:
        f.write('hello world\n')


@pytest.mark.buildconfigspec('cmd_vfs')
@pytest.mark.buildconfigspec('fs_ext4', 'fs_ext4l')
class TestVfsExt4:
    """Test VFS operations on an ext4 filesystem."""

    @pytest.fixture(scope='class')
    def fs_image(self, u_boot_config):
        """Create an ext4 filesystem image with test files."""
        fsh = FsHelper(u_boot_config, 'ext4', 64, 'vfs')
        fsh.setup()
        _populate_srcdir(fsh.srcdir)
        fsh.mk_fs()

        yield fsh.fs_img

        fsh.cleanup()


# Attach shared and ext4-specific test methods to TestVfsExt4
for _name in VFS_TESTS:
    setattr(TestVfsExt4, f'test_{_name}', _define_test(_name))
for _name in EXT4_TESTS:
    setattr(TestVfsExt4, f'test_{_name}',
            _define_test(_name, 'vfs_ext4'))


@pytest.mark.buildconfigspec('cmd_vfs')
@pytest.mark.buildconfigspec('fs_fat')
class TestVfsFat:
    """Test VFS operations on a FAT filesystem."""

    @pytest.fixture(scope='class')
    def fs_image(self, u_boot_config):
        """Create a FAT filesystem image with test files."""
        fsh = FsHelper(u_boot_config, 'fat32', 64, 'vfs')
        fsh.setup()
        _populate_srcdir(fsh.srcdir)
        fsh.mk_fs()

        yield fsh.fs_img

        fsh.cleanup()


# Attach shared test methods to TestVfsFat
for _name in VFS_TESTS:
    setattr(TestVfsFat, f'test_{_name}', _define_test(_name))
