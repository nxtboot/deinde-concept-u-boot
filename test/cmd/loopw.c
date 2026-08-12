// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the loopw command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Address used by these tests */
#define LOOPW_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)

/*
 * The command loops for ever and has no check for Ctrl-C, so only the paths
 * which refuse the command line can be tested. A command line which is
 * accepted never comes back
 */

/* Test 'loopw' with a missing argument */
static int cmd_test_loopw_usage(struct unit_test_state *uts)
{
	/* the value to write is needed as well as the address and the count */
	ut_asserteq(1, run_commandf("loopw %lx 1", (ulong)LOOPW_ADDR));
	ut_assert_nextline("loopw - infinite write loop on address range");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("loopw [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_loopw_usage, UTF_CONSOLE);

/* Test 'loopw' with a size suffix which it does not support */
static int cmd_test_loopw_size(struct unit_test_state *uts)
{
	/* an unknown suffix is refused without any output */
	ut_asserteq(1, run_commandf("loopw.x %lx 1 55", (ulong)LOOPW_ADDR));
	ut_assert_console_end();

	/* .s asks for a string, which this command has no use for */
	ut_asserteq(1, run_commandf("loopw.s %lx 1 55", (ulong)LOOPW_ADDR));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_loopw_size, UTF_CONSOLE);
