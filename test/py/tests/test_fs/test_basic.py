# SPDX-License-Identifier:      GPL-2.0+
# Copyright (c) 2018, Linaro Limited
# Author: Takahiro Akashi <takahiro.akashi@linaro.org>
#
# U-Boot File System:Basic Test

"""
This test verifies basic read/write operation on file system.

Tests are implemented in C (test/fs/fs_basic.c) and called from here.
Python handles filesystem image setup and environment variable configuration.
"""

import pytest
from fstest_defs import SMALL_FILE, BIG_FILE
from fstest_helpers import assert_fs_integrity


@pytest.mark.buildconfigspec('sandbox')
@pytest.mark.boardspec('!sandbox')
@pytest.mark.slow
class TestFsBasic:
    """Test basic filesystem operations via C unit tests."""

    def test_fs1(self, ubman, fs_obj_basic):
        """Test Case 1 - ls command, listing root and invalid directories"""
        fs_type, fs_img, _ = fs_obj_basic
        with ubman.log.section('Test Case 1 - ls'):
            ubman.run_ut('fs', 'fs_test_ls', fs_type=fs_type, fs_image=fs_img,
                         small=SMALL_FILE, big=BIG_FILE)

    def test_fs2(self, ubman, fs_obj_basic):
        """Test Case 2 - size command for a small file"""
        fs_type, fs_img, _ = fs_obj_basic
        with ubman.log.section('Test Case 2 - size (small)'):
            ubman.run_ut('fs', 'fs_test_size_small', fs_type=fs_type,
                         fs_image=fs_img, small=SMALL_FILE)

    def test_fs3(self, ubman, fs_obj_basic):
        """Test Case 3 - size command for a large file"""
        fs_type, fs_img, _ = fs_obj_basic
        with ubman.log.section('Test Case 3 - size (large)'):
            ubman.run_ut('fs', 'fs_test_size_big', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE)

    def test_fs4(self, ubman, fs_obj_basic):
        """Test Case 4 - load a small file, 1MB"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 4 - load (small)'):
            ubman.run_ut('fs', 'fs_test_load_small', fs_type=fs_type,
                         fs_image=fs_img, small=SMALL_FILE, md5val=md5val[0])

    def test_fs5(self, ubman, fs_obj_basic):
        """Test Case 5 - load, reading first 1MB of 3GB file"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 5 - load (first 1MB)'):
            ubman.run_ut('fs', 'fs_test_load_big_first', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE, md5val=md5val[1])

    def test_fs6(self, ubman, fs_obj_basic):
        """Test Case 6 - load, reading last 1MB of 3GB file"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 6 - load (last 1MB)'):
            ubman.run_ut('fs', 'fs_test_load_big_last', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE, md5val=md5val[2])

    def test_fs7(self, ubman, fs_obj_basic):
        """Test Case 7 - load, 1MB from the last 1MB in 2GB"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 7 - load (last 1MB in 2GB)'):
            ubman.run_ut('fs', 'fs_test_load_big_2g_last', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE, md5val=md5val[3])

    def test_fs8(self, ubman, fs_obj_basic):
        """Test Case 8 - load, reading first 1MB in 2GB"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 8 - load (first 1MB in 2GB)'):
            ubman.run_ut('fs', 'fs_test_load_big_2g_first', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE, md5val=md5val[4])

    def test_fs9(self, ubman, fs_obj_basic):
        """Test Case 9 - load, 1MB crossing 2GB boundary"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 9 - load (crossing 2GB boundary)'):
            ubman.run_ut('fs', 'fs_test_load_big_2g_cross', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE, md5val=md5val[5])

    def test_fs10(self, ubman, fs_obj_basic):
        """Test Case 10 - load, reading beyond file end"""
        fs_type, fs_img, _ = fs_obj_basic
        with ubman.log.section('Test Case 10 - load (beyond file end)'):
            ubman.run_ut('fs', 'fs_test_load_beyond', fs_type=fs_type,
                         fs_image=fs_img, big=BIG_FILE)

    def test_fs11(self, ubman, fs_obj_basic):
        """Test Case 11 - write"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 11 - write'):
            ubman.run_ut('fs', 'fs_test_write', fs_type=fs_type,
                         fs_image=fs_img, small=SMALL_FILE, md5val=md5val[0])
            assert_fs_integrity(fs_type, fs_img)

    def test_fs12(self, ubman, fs_obj_basic):
        """Test Case 12 - write to "." directory"""
        fs_type, fs_img, _ = fs_obj_basic
        with ubman.log.section('Test Case 12 - write (".")'):
            ubman.run_ut('fs', 'fs_test_write_dot', fs_type=fs_type,
                         fs_image=fs_img)
            assert_fs_integrity(fs_type, fs_img)

    def test_fs13(self, ubman, fs_obj_basic):
        """Test Case 13 - write to a file with '/./<filename>'"""
        fs_type, fs_img, md5val = fs_obj_basic
        with ubman.log.section('Test Case 13 - write  ("./<file>")'):
            ubman.run_ut('fs', 'fs_test_write_dotpath', fs_type=fs_type,
                         fs_image=fs_img, small=SMALL_FILE, md5val=md5val[0])
            assert_fs_integrity(fs_type, fs_img)
