// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the license command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Test 'license' printing the licence text */
static int cmd_test_license_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("license", 0));

	/* the text is uncompressed from the copy built into U-Boot */
	ut_assert_nextline("                    GNU GENERAL PUBLIC LICENSE");
	ut_assert_nextline("                       Version 2, June 1991");
	ut_assert_nextline_empty();
	ut_assert_skip_to_line("                            Preamble");

	/*
	 * Check the final line as well, since a text which stops early is the
	 * sign of a buffer too small for it
	 */
	ut_assert_skip_to_line("Public License instead of this License.");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_license_base, UTF_CONSOLE);

/* Test 'license' with a bad command line */
static int cmd_test_license_usage(struct unit_test_state *uts)
{
	/* the command takes no arguments at all */
	ut_asserteq(1, run_command("license gpl", 0));
	ut_assert_nextline("license - print GPL license text");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("license");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_license_usage, UTF_CONSOLE);
