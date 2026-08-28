// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the axi command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

#define AXI_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* the last line of the help text, which ends the usage message */
#define AXI_USAGE_LAST \
	"axi mw size addr value [count] - write data [value] to AXI device " \
	"at address [addr] and data width [size] (one of 8, 16, 32)"

/*
 * Select the sandbox bus. The current bus is a static, so the driver-model
 * state restored before each test leaves it pointing into the previous test's
 * tree; every test must choose the bus again.
 */
static int select_bus(struct unit_test_state *uts)
{
	ut_assertok(run_command("axi dev 0", 0));
	ut_assert_nextline("Setting bus to 0");
	ut_assert_console_end();

	return 0;
}

/*
 * Check the tail of a usage message. The help text ends with a newline, so a
 * blank line follows the last line of it.
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_skip_to_line(AXI_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}

/* Test 'axi bus' and 'axi dev' */
static int cmd_test_axi_base(struct unit_test_state *uts)
{
	ut_assertok(select_bus(uts));

	ut_assertok(run_command("axi dev", 0));
	ut_assert_nextline("Current bus is 0");
	ut_assert_console_end();

	/* the bus shows its children indented below it */
	ut_assertok(run_command("axi bus 0", 0));
	ut_assert_nextline("Bus 0:\taxi@0  (active)");
	ut_assert_nextline("  store@0");
	ut_assert_console_end();

	/*
	 * Listing every bus also picks up the AXI devices behind the emulated
	 * PCI bus, so stop at the sandbox one
	 */
	ut_assertok(run_command("axi bus", 0));
	ut_assert_skip_to_line("  store@0");
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_axi_base, AXI_TEST_FLAGS);

/* Test 'axi mw' and 'axi md' */
static int cmd_test_axi_md(struct unit_test_state *uts)
{
	ut_assertok(select_bus(uts));

	/* the store starts empty */
	ut_assertok(run_command("axi md 32 0 4", 0));
	ut_assert_nextline("00000000: 00000000 00000000 00000000 00000000  ................");
	ut_assert_console_end();

	ut_assertok(run_command("axi mw 32 0 deadbeef", 0));
	ut_assert_console_end();

	ut_assertok(run_command("axi md 32 0 4", 0));
	ut_assert_nextline("00000000: deadbeef 00000000 00000000 00000000  ................");
	ut_assert_console_end();

	/* the store answers big-endian, so the bytes come back in that order */
	ut_assertok(run_command("axi md 8 0 4", 0));
	ut_assert_nextline("00000000: de ad be ef                                      ....");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_axi_md, AXI_TEST_FLAGS);

/* Test that a count writes consecutive units of the chosen size */
static int cmd_test_axi_count(struct unit_test_state *uts)
{
	ut_assertok(select_bus(uts));

	ut_assertok(run_command("axi mw 8 40 aa 4", 0));
	ut_assert_console_end();

	ut_assertok(run_command("axi md 8 40 8", 0));
	ut_assert_nextline("00000040: aa aa aa aa 00 00 00 00                          ........");
	ut_assert_console_end();

	/* a 16-bit write steps two bytes at a time */
	ut_assertok(run_command("axi mw 16 60 beef 3", 0));
	ut_assert_console_end();

	ut_assertok(run_command("axi md 8 60 8", 0));
	ut_assert_nextline("00000060: be ef be ef be ef 00 00                          ........");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_axi_count, AXI_TEST_FLAGS);

/* Test 'axi' with a bad command line */
static int cmd_test_axi_usage(struct unit_test_state *uts)
{
	ut_assertok(select_bus(uts));

	/* a sub-command is required */
	ut_asserteq(1, run_command("axi", 0));
	ut_assert_nextline("axi - AXI sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assertok(check_usage(uts));

	/* an unknown sub-command is refused */
	ut_asserteq(1, run_command("axi nosuchsubcmd", 0));
	ut_assert_nextline("axi - AXI sub-system");
	ut_assertok(check_usage(uts));

	/* only 8, 16 and 32-bit accesses exist */
	ut_asserteq(1, run_command("axi md 64 0 1", 0));
	ut_assert_nextline("Unknown read size '64'");
	ut_assertok(check_usage(uts));

	ut_asserteq(1, run_command("axi mw 64 0 1", 0));
	ut_assert_nextline("Unknown write size '64'");
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_axi_usage, AXI_TEST_FLAGS);

/* Test 'axi' with a bus which is not there */
static int cmd_test_axi_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("axi bus 5", 0));
	ut_assert_nextline("Invalid bus 5: err=-19");
	ut_assert_console_end();

	ut_asserteq(1, run_command("axi dev 5", 0));
	ut_assert_nextline("Setting bus to 5");
	ut_assert_nextline("Failure changing bus number (-19)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_axi_missing, AXI_TEST_FLAGS);
