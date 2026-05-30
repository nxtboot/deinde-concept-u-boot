# SPDX-License-Identifier: GPL-2.0+
# Copyright (c) 2025 Simon Glass
# Written by Simon Glass <sjg@chromium.org>

"""Test U-Boot Makefile dependency tracking

This test verifies that Makefile rules properly track dependencies and rebuild
targets when dependency files change. It tests rules that had FORCE removed
to rely on proper dependency tracking instead.

Tests cover:
- hwids_to_dtsi: Hardware ID mapping to device tree source
- $(obj)/%.dtbo: Device tree source overlay to binary overlay compilation
- ESL dependency chains: Certificate -> ESL file -> DTSI file transformation
"""

import os
import time
from types import SimpleNamespace

import pytest
import utils

# Time to wait between file modifications to ensure different timestamps
TIMESTAMP_DELAY = 2


def _info(env, message):
    """Log an informational message

    Args:
        env (SimpleNamespace): Test environment with ubman logger
        message (str): Message to log
    """
    env.ubman.log.info(message)

def _run_make(env, message, mtime_file=None):
    """Run a real U-Boot build command with logging

    Args:
        env (SimpleNamespace): Test environment with ubman logger and paths
        message (str): Log message describing the build
        mtime_file (str, optional): File path to get mtime from. If None,
            returns None.

    Returns:
        float or None: File modification time of mtime_file, or None if
            mtime_file not provided
    """
    _info(env, message)

    # Build all targets using real U-Boot makefile
    utils.run_and_log(env.ubman, ['make', '-j30', 'all'], cwd=env.build_dir)

    # Return modification time if mtime_file is specified
    if mtime_file:
        return os.path.getmtime(mtime_file)
    return None


def _setup_hwids_env(ubman):
    """Set up test environment for hwids dependency tracking

    Uses real U-Boot build

    Args:
        ubman (ConsoleBase): ubman fixture

    Returns:
        SimpleNamespace: Test environment with paths and config
    """
    # CHID is enabled in sandbox config

    # Use the real sandbox hwids directory
    hwids_dir = os.path.join(ubman.config.source_dir, 'board/sandbox/hwids')
    hwidmap_file = os.path.join(hwids_dir, 'compatible.hwidmap')

    # The hwids DTSI file gets created in test/fdt_overlay/ directory
    # when building test DTBs that include hwids
    test_target = os.path.join(ubman.config.build_dir,
                               'test/fdt_overlay/hwids.dtsi')

    # Use the known hwid txt files for testing
    hwid_txt_file = os.path.join(hwids_dir, 'test-device-1.txt')
    second_test_target = os.path.join(hwids_dir, 'test-device-2.txt')

    return SimpleNamespace(
        ubman=ubman,
        build_dir=ubman.config.build_dir,
        source_dir=ubman.config.source_dir,
        hwids_dir=hwids_dir,
        hwidmap_file=hwidmap_file,
        hwid_txt_file=hwid_txt_file,
        second_test_target=second_test_target,
        test_target=test_target
    )


def _check_initial_build(env):
    """Test initial build and no-change rebuild behavior

    Args:
        env (SimpleNamespace): Test environment with paths and ubman logger

    Returns:
        tuple: (initial_mtime, second_mtime) modification times
    """
    # Clean the test target
    if os.path.exists(env.test_target):
        os.remove(env.test_target)
    initial_mtime = _run_make(env, 'build hwids target', env.test_target)

    _info(env, f'Initial target created at {initial_mtime}')

    # Wait a bit to ensure timestamp differences
    time.sleep(TIMESTAMP_DELAY)

    # Run build again - target should NOT be rebuilt (no dependencies changed)
    mtime = _run_make(env, 'build again without changes', env.test_target)
    assert mtime == initial_mtime, \
        f'Target should not rebuild when dependencies unchanged ' \
        f'(initial: {initial_mtime}, second: {mtime})'

    return initial_mtime, mtime


def _check_hwidmap_change(env, previous_mtime):
    """Test that changing hwidmap file triggers rebuild

    Args:
        env (SimpleNamespace): Test environment with paths and ubman logger
        previous_mtime (float): Previous modification time to compare against

    Returns:
        float: New modification time after rebuild
    """
    time.sleep(TIMESTAMP_DELAY)

    # Touch the hwidmap file to update its timestamp
    os.utime(env.hwidmap_file)

    # Build again - target SHOULD be rebuilt due to hwidmap change
    mtime = _run_make(env, 'build after hwidmap change', env.test_target)
    assert mtime > previous_mtime, \
        f'Target should rebuild when hwidmap changes ' \
        f'(previous: {previous_mtime}, new: {mtime})'

    return mtime


def _check_txt_changes(env, previous_mtime):
    """Test that changing txt files triggers rebuild

    Args:
        env (SimpleNamespace): Test environment with paths and ubman logger
        previous_mtime (float): Previous modification time to compare against

    Returns:
        float: Final modification time after all changes
    """
    # Test existing txt file modification
    time.sleep(TIMESTAMP_DELAY)

    # Touch the txt file to update its timestamp
    os.utime(env.hwid_txt_file)

    modified_mtime = _run_make(env, 'build after txt change', env.test_target)
    assert modified_mtime > previous_mtime, \
        f'Target should rebuild when txt file changes ' \
        f'(previous: {previous_mtime}, modified: {modified_mtime})'

    # Test modification of a second txt file to verify dependency tracking
    # across multiple files
    time.sleep(TIMESTAMP_DELAY)

    # Touch second txt file to update its timestamp
    os.utime(env.second_test_target)

    mtime = _run_make(env, 'build after second txt change', env.test_target)
    assert mtime > modified_mtime, \
        f'Target should rebuild when second txt file changes ' \
        f'(modified: {modified_mtime}, final: {mtime})'

    return mtime


def _setup_dtbo_env(ubman):
    """Set up test environment for dtbo dependency tracking

    Uses real U-Boot build

    Args:
        ubman (ConsoleBase): ubman fixture

    Returns:
        SimpleNamespace: DTBO test environment with paths and config
    """
    # Use the existing test overlay files from U-Boot source
    dtso_file = os.path.join(ubman.config.source_dir,
                             'test/fdt_overlay/test-fdt-overlay.dtso')

    # The DTBO gets built and compiled into .o file during test build
    # We track the .o file since the .dtbo is an intermediate file
    dtbo_file = os.path.join(ubman.config.build_dir,
                             'test/fdt_overlay/test-fdt-overlay.dtbo.o')

    return SimpleNamespace(
        ubman=ubman,
        build_dir=ubman.config.build_dir,
        source_dir=ubman.config.source_dir,
        dtso_file=dtso_file,
        dtbo_target=dtbo_file
    )



def _check_dtbo_build(env):
    """Test initial dtbo build and no-change rebuild behavior

    Args:
        env (SimpleNamespace): Test environment with paths and ubman logger

    Returns:
        float: Initial modification time of dtbo file
    """
    # Clean the DTBO target and related files
    dtbo_base = env.dtbo_target.replace('.dtbo.o', '.dtbo')
    for f in [env.dtbo_target, dtbo_base, dtbo_base + '.S']:
        if os.path.exists(f):
            os.remove(f)
    initial_mtime = _run_make(env, 'build dtbo target', env.dtbo_target)

    _info(env, f'Initial dtbo created at {initial_mtime}')

    # Wait a bit to ensure timestamp differences
    time.sleep(TIMESTAMP_DELAY)

    # Run build again - target should NOT be rebuilt (no dependencies changed)
    mtime = _run_make(env, 'build dtbo again', env.dtbo_target)
    assert mtime == initial_mtime, \
        f'DTBO should not rebuild when dtso unchanged ' \
        f'(initial: {initial_mtime}, second: {mtime})'

    return initial_mtime


def _check_dtso_change(env, previous_mtime):
    """Test that changing dtso file triggers dtbo rebuild

    Args:
        env (SimpleNamespace): Test environment with paths and ubman logger
        previous_mtime (float): Previous modification time to compare against

    Returns:
        float: New modification time after dtso change and rebuild
    """
    time.sleep(TIMESTAMP_DELAY)

    # Touch the dtso file to update its timestamp
    os.utime(env.dtso_file)

    # Build again - target SHOULD be rebuilt due to dtso change
    mtime = _run_make(env, 'build dtbo after dtso change', env.dtbo_target)
    assert mtime > previous_mtime, \
        f'DTBO should rebuild when dtso changes ' \
        f'(previous: {previous_mtime}, new: {mtime})'

    return mtime


@pytest.mark.boardspec('sandbox')
@pytest.mark.slow
def test_dep_hwids(ubman):
    """Test that Makefile dependency tracking works without FORCE

    This test verifies that the hwids_to_dtsi rule (which had FORCE removed)
    still properly rebuilds when dependency files are modified.
    """
    env = _setup_hwids_env(ubman)

    _info(env, 'initial build and no-change rebuild')
    _, second_mtime = _check_initial_build(env)

    # Initial build should have created hwids files

    _info(env, 'hwidmap file change triggers rebuild')
    third_mtime = _check_hwidmap_change(env, second_mtime)

    _info(env, 'txt file changes trigger rebuild')
    _check_txt_changes(env, third_mtime)


@pytest.mark.boardspec('sandbox')
@pytest.mark.slow
def test_dep_dtbo(ubman):
    """Test that dtbo dependency tracking works without FORCE

    This test verifies that the $(obj)/%.dtbo rule properly rebuilds
    when the corresponding .dtso source file is modified.
    """
    env = _setup_dtbo_env(ubman)

    _info(env, 'initial dtbo build and no-change rebuild')
    first_mtime = _check_dtbo_build(env)

    # Initial build should have created DTBO files

    _info(env, 'dtso file change triggers dtbo rebuild')
    _check_dtso_change(env, first_mtime)
