# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the CRAMFS commands
"""

import os
import re
import tempfile
from subprocess import run
import pytest

# Address to load the CRAMFS image to, and one which holds no image
ADDR = 0x1000000
BAD_ADDR = 0x2000000

# Address to load a file out of the image to
DEST = 0x3000000

# Contents of the two files in the image
HELLO = 'Hello, world!\n'
DEEP = 'down here\n'

# The mode string varies with the umask of the source directory, so only the
# leading character, the size and the name are pinned down
HELLO_LINE = re.compile(r'^\s*-[rwx-]{9}\s+14 hello\.txt$')
SUBDIR_LINE = re.compile(r'^\s*d[rwx-]{9}\s+\d+ subdir$')
DEEP_LINE = re.compile(r'^\s*-[rwx-]{9}\s+10 deep\.txt$')

class CramfsImage:
    """Create a CRAMFS image holding a file and a subdirectory

    Usage:
        with CramfsImage() as img:
            # img.fname is the image, on the host filesystem

        The image and the directory it was built from are erased after the
        'with' statement.
    """
    def __init__(self):
        # Some distributions do not add /sbin to the default PATH, where
        # mkfs.cramfs lives
        if '/sbin' not in os.environ['PATH'].split(os.pathsep):
            os.environ['PATH'] += os.pathsep + '/sbin'
        self.tmpdir = None
        self.fname = None

    def __enter__(self):
        self.tmpdir = tempfile.TemporaryDirectory('cramfs')
        srcdir = os.path.join(self.tmpdir.name, 'src')
        os.mkdir(srcdir)
        os.mkdir(os.path.join(srcdir, 'subdir'))
        with open(os.path.join(srcdir, 'hello.txt'), 'w',
                  encoding='ascii') as outf:
            outf.write(HELLO)
        with open(os.path.join(srcdir, 'subdir', 'deep.txt'), 'w',
                  encoding='ascii') as outf:
            outf.write(DEEP)

        self.fname = os.path.join(self.tmpdir.name, 'test.cramfs')

        # mkfs.cramfs warns about truncating the gids, so keep it quiet
        run(['mkfs.cramfs', srcdir, self.fname], capture_output=True,
            check=True)
        return self

    def __exit__(self, extype, value, traceback):
        self.tmpdir.cleanup()

def listing(out):
    """Split a listing into its entries

    Args:
        out (str): Output from the command

    Returns:
        list of str: One entry per line, with blank lines dropped, since the
            console can put one between the entries
    """
    return [line for line in out.splitlines() if line.strip()]

def load_image(ubman, img):
    """Load a CRAMFS image into memory and point cramfsaddr at it

    Args:
        ubman -- U-Boot console
        img (CramfsImage): Image to load
    """
    ubman.run_command(f'host load hostfs - {ADDR:x} {img.fname}')
    ubman.run_command(f'setenv cramfsaddr {ADDR:x}')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_base(ubman):
    """Check that cramfsls lists the root of a CRAMFS image

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        lines = listing(ubman.run_command('cramfsls'))
        assert '0' == ubman.run_command('echo $?')
        assert 2 == len(lines)
        assert HELLO_LINE.match(lines[0])
        assert SUBDIR_LINE.match(lines[1])

        # The default path is the root, so naming it is the same
        assert lines == listing(ubman.run_command('cramfsls /'))

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_dir(ubman):
    """Check that cramfsls lists a subdirectory

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        lines = listing(ubman.run_command('cramfsls subdir'))
        assert '0' == ubman.run_command('echo $?')
        assert 1 == len(lines)
        assert DEEP_LINE.match(lines[0])

        assert lines == listing(ubman.run_command('cramfsls /subdir'))

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_file(ubman):
    """Check that naming a file lists just that file

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        lines = listing(ubman.run_command('cramfsls hello.txt'))
        assert '0' == ubman.run_command('echo $?')
        assert 1 == len(lines)
        assert HELLO_LINE.match(lines[0])

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_missing(ubman):
    """Check that cramfsls reports a path which is not in the image

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command('cramfsls missing')
        assert '1' == ubman.run_command('echo $?')
        assert "can't find corresponding entry" == out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_no_image(ubman):
    """Check that cramfsls refuses an address which holds no image

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)
        ubman.run_command(f'setenv cramfsaddr {BAD_ADDR:x}')

        # Nothing is printed, so the return value is the only sign
        assert '' == ubman.run_command('cramfsls')
        assert '1' == ubman.run_command('echo $?')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_unset(ubman):
    """Check that cramfsls reports a missing cramfsaddr rather than crashing

    Args:
        ubman -- U-Boot console
    """
    ubman.run_command('setenv cramfsaddr')

    out = ubman.run_command('cramfsls')
    assert '1' == ubman.run_command('echo $?')
    assert "Environment variable 'cramfsaddr' is not set" == out

    # The command is still there afterwards
    assert 'cramfsls' in ubman.run_command('help cramfsls')

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_option(ubman):
    """Check that cramfsls refuses an option, since it has none

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command('cramfsls -a')
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsls_args(ubman):
    """Check that cramfsls rejects too many arguments

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command('cramfsls subdir extra')
        assert '1' == ubman.run_command('echo $?')
        assert 'Usage:' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsload_base(ubman):
    """Check that cramfsload reads a file out of a CRAMFS image

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command(f'cramfsload {DEST:x} hello.txt')
        assert '0' == ubman.run_command('echo $?')
        assert f'{len(HELLO)} bytes loaded to 0x{DEST:x}' in out

        # the size is reported in filesize, in hex
        assert f'filesize={len(HELLO):x}' in ubman.run_command(
            'printenv filesize')

        # the file really is there, in the ASCII column of the dump
        out = ubman.run_command(f'md.b {DEST:x} {len(HELLO):x}')
        assert HELLO.strip() in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsload_subdir(ubman):
    """Check that cramfsload reads a file from a subdirectory

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command(f'cramfsload {DEST:x} subdir/deep.txt')
        assert f'{len(DEEP)} bytes loaded to 0x{DEST:x}' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsload_hex(ubman):
    """Check that the address is read as hexadecimal

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        # 2000000 means 0x2000000, not two million
        out = ubman.run_command('cramfsload 2000000 hello.txt')
        assert 'loaded to 0x2000000' in out

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_cramfs')
def test_cramfsload_missing(ubman):
    """Check that cramfsload reports a file which is not in the image

    Args:
        ubman -- U-Boot console
    """
    with CramfsImage() as img:
        load_image(ubman, img)

        out = ubman.run_command(f'cramfsload {DEST:x} nofile.txt')
        assert '1' == ubman.run_command('echo $?')
        assert 'CRAMFS LOAD ERROR' in out
