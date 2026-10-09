// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the bootctl command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <bootctl.h>
#include <dm.h>
#include <test/cmd.h>
#include <test/ut.h>

#define SEP		"---  --------------  --------------  --------------------"
#define LOGIC_LINE	\
	"  0  bootctl         bootctrl        Controls the boot process"

/* check the usage message, which every error path prints */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("bootctl - Boot control");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("bootctl list      - list bootctl drivers");
	ut_assert_nextline("bootctl run       - run a boot");
	ut_assert_console_end();

	return 0;
}

/* Test 'bootctl list' */
static int cmd_test_bootctl_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("bootctl list", 0));
	ut_assert_nextline("Seq  Name            Type            Description");
	ut_assert_nextline(SEP);
	ut_assert_nextline(LOGIC_LINE);
	ut_assert_nextline(SEP);
	ut_assert_nextline("(1 driver)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bootctl_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/*
 * Test the error paths. There is no test for 'bootctl run' since it starts a
 * boot and does not return.
 */
static int cmd_test_bootctl_usage(struct unit_test_state *uts)
{
	/* no sub-command */
	ut_asserteq(1, run_command("bootctl", 0));
	ut_assertok(check_usage(uts));

	/* a sub-command which does not exist */
	ut_asserteq(1, run_command("bootctl wibble", 0));
	ut_assertok(check_usage(uts));

	/* too many arguments for a sub-command which takes none */
	ut_asserteq(1, run_command("bootctl list extra", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_bootctl_usage, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);
