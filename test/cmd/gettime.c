// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the gettime command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <time.h>
#include <vsprintf.h>
#include <linux/delay.h>
#include <test/cmd.h>
#include <test/ut.h>

/**
 * read_val() - Read the number at the end of the next console line
 *
 * @uts: Test state
 * @prefix: Text expected at the start of the line
 * @valp: Returns the decimal value which follows it
 * Return: 0 if OK, -ve on error
 */
static int read_val(struct unit_test_state *uts, const char *prefix,
		    ulong *valp)
{
	ut_assert_nextlinen("%s", prefix);
	*valp = dectoul(uts->actual_str + strlen(prefix), NULL);

	return 0;
}

/* Test 'gettime' showing the timer value */
static int cmd_test_gettime_base(struct unit_test_state *uts)
{
	ulong before, after, val, secs, rem, hz;

	before = get_timer(0);
	ut_assertok(run_command("gettime", 0));
	after = get_timer(0);

	ut_assertok(read_val(uts, "Timer val: ", &val));
	ut_assertok(read_val(uts, "Seconds : ", &secs));
	ut_assertok(read_val(uts, "Remainder : ", &rem));
	ut_assertok(read_val(uts, "sys_hz = ", &hz));
	ut_assert_console_end();

	/* the value is the timer read at the time the command ran */
	ut_assert(val >= before);
	ut_assert(val <= after);

	/* the other lines are that value split up by the tick rate */
	ut_asserteq(CONFIG_SYS_HZ, hz);
	ut_asserteq(val / CONFIG_SYS_HZ, secs);
	ut_asserteq(val % CONFIG_SYS_HZ, rem);

	return 0;
}
CMD_TEST(cmd_test_gettime_base, UTF_CONSOLE);

/* Test that the timer moves forwards */
static int cmd_test_gettime_advance(struct unit_test_state *uts)
{
	ulong first, second, dummy;

	ut_assertok(run_command("gettime", 0));
	ut_assertok(read_val(uts, "Timer val: ", &first));
	ut_assertok(read_val(uts, "Seconds : ", &dummy));
	ut_assertok(read_val(uts, "Remainder : ", &dummy));
	ut_assertok(read_val(uts, "sys_hz = ", &dummy));

	mdelay(20);

	ut_assertok(run_command("gettime", 0));
	ut_assertok(read_val(uts, "Timer val: ", &second));
	ut_assertok(read_val(uts, "Seconds : ", &dummy));
	ut_assertok(read_val(uts, "Remainder : ", &dummy));
	ut_assertok(read_val(uts, "sys_hz = ", &dummy));
	ut_assert_console_end();

	/* the command reads the timer afresh each time */
	ut_assert(second >= first + 20);

	return 0;
}
CMD_TEST(cmd_test_gettime_advance, UTF_CONSOLE);

/* Test 'gettime' with an argument, which it does not take */
static int cmd_test_gettime_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("gettime now", 0));
	ut_assert_nextline("gettime - get timer val elapsed");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("gettime get time elapsed");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_gettime_usage, UTF_CONSOLE);
