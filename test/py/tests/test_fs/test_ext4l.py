# SPDX-License-Identifier: GPL-2.0+
# Copyright 2025 Canonical Ltd
# Written by Simon Glass <simon.glass@canonical.com>
#
# Test for ext4l filesystem driver

"""
Test ext4l filesystem probing via C unit test.
"""

import os
from subprocess import CalledProcessError, check_call
from tempfile import NamedTemporaryFile

import pytest

from tests.fs_helper import FsHelper


@pytest.mark.buildconfigspec('sandbox')
@pytest.mark.buildconfigspec('fs_ext4l')
class TestExt4l:
    """Test ext4l filesystem operations."""

    @pytest.fixture(scope='class')
    def ext4_image(self, u_boot_config):
        """Create an ext4 filesystem image for testing.

        Args:
            u_boot_config (u_boot_config): U-Boot configuration.

        Yields:
            str: Path to the ext4 image file.
        """
        image_path = os.path.join(u_boot_config.persistent_data_dir,
                                  'ext4l_test.img')
        try:
            # Create a 64MB ext4 image
            check_call(f'dd if=/dev/zero of={image_path} bs=1M count=64 2>/dev/null',
                       shell=True)
            check_call(f'mkfs.ext4 -q {image_path}', shell=True)

            # Add test files using debugfs (no mount required)
            with NamedTemporaryFile(mode='w', delete=False) as tmp:
                tmp.write('hello world\n')
                tmp_path = tmp.name
            try:
                # Add a regular file
                check_call(f'debugfs -w {image_path} '
                           f'-R "write {tmp_path} testfile.txt" 2>/dev/null',
                           shell=True)
                # Add a subdirectory
                check_call(f'debugfs -w {image_path} '
                           f'-R "mkdir subdir" 2>/dev/null',
                           shell=True)
                # Add a file in the subdirectory
                check_call(f'debugfs -w {image_path} '
                           f'-R "write {tmp_path} subdir/nested.txt" 2>/dev/null',
                           shell=True)
                # Add a symlink
                check_call(f'debugfs -w {image_path} '
                           f'-R "symlink link.txt testfile.txt" 2>/dev/null',
                           shell=True)
            finally:
                os.unlink(tmp_path)
        except CalledProcessError:
            pytest.skip('Failed to create ext4 image')

        yield image_path

        # Cleanup (skip if --persist flag is set)
        if not u_boot_config.persist and os.path.exists(image_path):
            os.remove(image_path)

    def test_probe(self, ubman, ext4_image):
        """Test that ext4l can probe an ext4 filesystem."""
        with ubman.log.section('Test ext4l probe'):
            ubman.run_ut('fs', 'fs_test_ext4l_probe', fs_image=ext4_image)

    def test_msgs(self, ubman, ext4_image):
        """Test that ext4l_msgs env var produces mount messages."""
        with ubman.log.section('Test ext4l msgs'):
            ubman.run_ut('fs', 'fs_test_ext4l_msgs', fs_image=ext4_image)

    @pytest.mark.boardspec('!sandbox')
    def test_ls(self, ubman, ext4_image):
        """Test that ext4l can list directory contents."""
        with ubman.log.section('Test ext4l ls'):
            ubman.run_ut('fs', 'fs_test_ext4l_ls', fs_image=ext4_image)

    def test_opendir(self, ubman, ext4_image):
        """Test that ext4l can iterate directory entries."""
        with ubman.log.section('Test ext4l opendir'):
            ubman.run_ut('fs', 'fs_test_ext4l_opendir', fs_image=ext4_image)

    def test_exists(self, ubman, ext4_image):
        """Test that ext4l_exists reports file existence correctly."""
        with ubman.log.section('Test ext4l exists'):
            ubman.run_ut('fs', 'fs_test_ext4l_exists', fs_image=ext4_image)

    def test_size(self, ubman, ext4_image):
        """Test that ext4l_size reports file size correctly."""
        with ubman.log.section('Test ext4l size'):
            ubman.run_ut('fs', 'fs_test_ext4l_size', fs_image=ext4_image)

    def test_read(self, ubman, ext4_image):
        """Test that ext4l can read file contents."""
        with ubman.log.section('Test ext4l read'):
            ubman.run_ut('fs', 'fs_test_ext4l_read', fs_image=ext4_image)

    def test_uuid(self, ubman, ext4_image):
        """Test that ext4l can return the filesystem UUID."""
        with ubman.log.section('Test ext4l uuid'):
            ubman.run_ut('fs', 'fs_test_ext4l_uuid', fs_image=ext4_image)

    def test_statfs(self, ubman, ext4_image):
        """Test that ext4l can return filesystem statistics."""
        with ubman.log.section('Test ext4l statfs'):
            ubman.run_ut('fs', 'fs_test_ext4l_statfs', fs_image=ext4_image)

    @pytest.mark.boardspec('!sandbox')
    def test_fsinfo(self, ubman, ext4_image):
        """Test that fsinfo command displays filesystem statistics."""
        with ubman.log.section('Test ext4l fsinfo'):
            ubman.run_ut('fs', 'fs_test_ext4l_fsinfo', fs_image=ext4_image)

    def test_write(self, ubman, ext4_image):
        """Test that ext4l can write file contents."""
        with ubman.log.section('Test ext4l write'):
            ubman.run_ut('fs', 'fs_test_ext4l_write', fs_image=ext4_image)

    def test_unlink(self, ubman, ext4_image):
        """Test that ext4l can delete files."""
        with ubman.log.section('Test ext4l unlink'):
            ubman.run_ut('fs', 'fs_test_ext4l_unlink', fs_image=ext4_image)

    def test_mkdir(self, ubman, ext4_image):
        """Test that ext4l can create directories."""
        with ubman.log.section('Test ext4l mkdir'):
            ubman.run_ut('fs', 'fs_test_ext4l_mkdir', fs_image=ext4_image)

    def test_ln(self, ubman, ext4_image):
        """Test that ext4l can create symbolic links."""
        with ubman.log.section('Test ext4l ln'):
            ubman.run_ut('fs', 'fs_test_ext4l_ln', fs_image=ext4_image)

    def test_rename(self, ubman, ext4_image):
        """Test that ext4l can rename files and directories."""
        with ubman.log.section('Test ext4l rename'):
            ubman.run_ut('fs', 'fs_test_ext4l_rename', fs_image=ext4_image)

    @pytest.fixture(scope='class')
    def dual_images(self, u_boot_config):
        """Create two ext4 images with different content.

        Image A contains id.txt with "alpha\\n".
        Image B contains id.txt with "bravo\\n".

        Yields:
            tuple: (path_a, path_b) paths to the two images
        """
        helpers = []
        for label, content in [('a', 'alpha'), ('b', 'bravo')]:
            fsh = FsHelper(u_boot_config, 'ext4', 64, f'dual_{label}')
            fsh.setup()
            with open(os.path.join(fsh.srcdir, 'id.txt'), 'w') as f:
                f.write(content + '\n')
            fsh.mk_fs()
            helpers.append(fsh)

        yield tuple(fsh.fs_img for fsh in helpers)

        for fsh in helpers:
            fsh.cleanup()

    @pytest.mark.buildconfigspec('cmd_vfs')
    def test_dual_mount(self, ubman, dual_images):
        """Test that two ext4l filesystems can be mounted simultaneously."""
        image_a, image_b = dual_images
        with ubman.log.section('Test ext4l dual mount'):
            ubman.run_ut('fs', 'fs_test_ext4l_dual_mount',
                         fs_image_a=image_a, fs_image_b=image_b)
