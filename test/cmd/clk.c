// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the clk command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

#define CLK_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* the deepest line of the tree, three levels below the oscillator */
#define CLK_DEEPEST \
	" 80000000             0        |           `-- usdhc2_sel"

/* the last SCMI clock, which comes after everything else */
#define CLK_SCMI_LAST	" 1000                 0        |-- scmi-2"

/* Test 'clk dump' listing the clock tree */
static int cmd_test_clk_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("clk dump", 0));
	ut_assert_nextline(" Rate               Usecnt      Name");
	ut_assert_nextline("------------------------------------------");

	/* the sandbox clock and the fixed clock are both at the top level */
	ut_assert_nextline(" 321                  0        |-- clk-sbox");
	ut_assert_nextline(" 1234                 0        |-- clk-fixed");

	/* a clock with a parent is shown under it, indented */
	ut_assert_nextline(" 20000000             0        |-- osc");
	ut_assert_nextline(" 480000000            0        |   `-- pll3_usb_otg");

	/*
	 * the SCMI clocks come after everything else, but only
	 * sandbox_defconfig has an SCMI agent, so with any other config the
	 * dump stops short of them
	 */
	if (IS_ENABLED(CONFIG_CLK_SCMI)) {
		ut_assert_skip_to_line(CLK_SCMI_LAST);
		ut_assert_console_end();
	} else {
		ut_assert_skip_to_line(CLK_DEEPEST);
		console_record_reset();
	}

	return 0;
}
CMD_TEST(cmd_test_clk_base, CLK_TEST_FLAGS);

/* Test 'clk setfreq' changing a rate */
static int cmd_test_clk_setfreq(struct unit_test_state *uts)
{
	/* sandbox reports the rate which was in force before the change */
	ut_assertok(run_command("clk setfreq clk-sbox 500", 0));
	ut_assert_nextline("set_rate returns 321");
	ut_assert_console_end();

	/* the new rate is the one now shown by the dump */
	ut_assertok(run_command("clk dump", 0));
	ut_assert_skip_to_line(" 500                  0        |-- clk-sbox");
	console_record_reset();

	/* setting it again reports the rate just set */
	ut_assertok(run_command("clk setfreq clk-sbox 321", 0));
	ut_assert_nextline("set_rate returns 500");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_clk_setfreq, CLK_TEST_FLAGS);

/* Test 'clk' with a clock which is not there */
static int cmd_test_clk_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("clk setfreq nosuchclock 500", 0));
	ut_assert_nextline("clock 'nosuchclock' not found.");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_clk_missing, CLK_TEST_FLAGS);

/* Test 'clk' with a bad command line */
static int cmd_test_clk_usage(struct unit_test_state *uts)
{
	/* a sub-command is required */
	ut_asserteq(1, run_command("clk", 0));
	ut_assert_nextline("clk - CLK sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line("clk setfreq [clk] [freq] - Set clock frequency");
	ut_assert_console_end();

	/* an unknown sub-command is refused */
	ut_asserteq(1, run_command("clk nosuchsubcmd", 0));
	ut_assert_nextline("clk - CLK sub-system");
	ut_assert_skip_to_line("clk setfreq [clk] [freq] - Set clock frequency");
	ut_assert_console_end();

	/* setfreq needs both a clock and a frequency */
	ut_asserteq(1, run_command("clk setfreq clk-sbox", 0));
	ut_assert_nextline("clk - CLK sub-system");
	ut_assert_skip_to_line("clk setfreq [clk] [freq] - Set clock frequency");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_clk_usage, CLK_TEST_FLAGS);
