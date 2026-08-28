# SPDX-License-Identifier:      GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

""" Tests for the axi command
"""

import pytest

@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('cmd_axi')
def test_axi_no_bus(ubman):
    """Check that 'axi mw' refuses to write before a bus is selected

    The current bus is held in a static, so this can only be seen in a fresh
    U-Boot, before anything has run 'axi dev'.

    Args:
        ubman -- U-Boot console
    """
    ubman.restart_uboot()
    output = ubman.run_command('axi mw 32 0 12345678')
    assert 'No AXI bus selected' in output
    assert ubman.run_command('echo $?') == '1'
