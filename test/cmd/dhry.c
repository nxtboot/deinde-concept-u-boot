// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the dhry command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <vsprintf.h>
#include <linux/stringify.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * Iterations which take long enough to be timed on sandbox, which manages
 * something over 20000 DMIPS, while staying well under a tenth of a second
 */
#define ITERATIONS	300000

/*
 * check_report() - Check the line the command prints for a timed run
 *
 * The duration and the rates depend on how fast the machine is, so only their
 * shape can be checked.
 *
 * @uts: Test state
 * @iterations: Count the command is expected to report
 */
static int check_report(struct unit_test_state *uts, int iterations)
{
	char pat[64];

	snprintf(pat, sizeof(pat),
		 "^%d iterations in [0-9]+ ms: [0-9]+/s, [0-9]+ DMIPS$",
		 iterations);
	ut_assert_nextline_regex(pat);
	ut_assert_console_end();

	return 0;
}

/* Test that a run long enough to time reports a rate */
static int cmd_test_dhry_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("dhry " __stringify(ITERATIONS), 0));
	ut_assertok(check_report(uts, ITERATIONS));

	return 0;
}
CMD_TEST(cmd_test_dhry_base, UTF_CONSOLE);

/* Test that the iteration count defaults to a million */
static int cmd_test_dhry_default(struct unit_test_state *uts)
{
	ut_assertok(run_command("dhry", 0));
	ut_assertok(check_report(uts, 1000000));

	return 0;
}
CMD_TEST(cmd_test_dhry_default, UTF_CONSOLE);

/*
 * Test that a run too short to time is reported rather than divided by
 *
 * Whether a very short run registers a millisecond at all is a race against
 * the timer, so either message is allowed here. What is being checked is that
 * the command comes back: the duration used to go into lldiv() unchecked, so a
 * zero divided by zero and sandbox died with SIGFPE, printing nothing.
 */
static int cmd_test_dhry_short(struct unit_test_state *uts)
{
	ut_assert(run_command("dhry 0", 0) <= 1);
	ut_assert_nextlinen("0 iterations ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_dhry_short, UTF_CONSOLE);

/* Test that the command refuses more arguments than it has room for */
static int cmd_test_dhry_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("dhry 1 2", 0));
	ut_assert_nextline("dhry - [iterations] - run dhrystone benchmark");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("dhry ");
	ut_assert_nextlinen("    - run the Dhrystone 2.1 benchmark");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_dhry_usage, UTF_CONSOLE);
