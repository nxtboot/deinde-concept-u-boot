// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the loop command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Address used by these tests */
#define LOOP_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)

/*
 * The command loops for ever and has no check for Ctrl-C, so only the paths
 * which refuse the command line can be tested. A command line which is
 * accepted never comes back
 */

/* Test 'loop' with a missing argument */
static int cmd_test_loop_usage(struct unit_test_state *uts)
{
	/* the address on its own is not enough, since a count is needed too */
	ut_asserteq(1, run_commandf("loop %lx", (ulong)LOOP_ADDR));
	ut_assert_nextline("loop - infinite loop on address range");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("loop [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_loop_usage, UTF_CONSOLE);

/* Test 'loop' with a size suffix which it does not support */
static int cmd_test_loop_size(struct unit_test_state *uts)
{
	/* an unknown suffix is refused without any output */
	ut_asserteq(1, run_commandf("loop.x %lx 1", (ulong)LOOP_ADDR));
	ut_assert_console_end();

	/* .s asks for a string, which this command has no use for */
	ut_asserteq(1, run_commandf("loop.s %lx 1", (ulong)LOOP_ADDR));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_loop_size, UTF_CONSOLE);
