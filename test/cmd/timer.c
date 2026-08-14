// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the timer command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <time.h>
#include <vsprintf.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * Time which the test itself is allowed to take between the reference point
 * and the reading, in milliseconds. The command prints real elapsed time as
 * well as the offset added here, so the reading cannot be checked exactly
 */
#define SLACK_MS	100

/**
 * read_elapsed() - Read the time printed by 'timer get'
 *
 * @uts: Test state
 * @msp: Returns the elapsed time in milliseconds
 * Return: 0 if OK, -ve on error
 */
static int read_elapsed(struct unit_test_state *uts, ulong *msp)
{
	char line[32];
	char *dot, *end;
	ulong secs, ms;

	ut_assert(console_record_readline(line, sizeof(line)) > 0);

	/* the time is printed as seconds with exactly three decimal places */
	secs = dectoul(line, &dot);
	ut_asserteq('.', *dot);
	ms = dectoul(dot + 1, &end);
	ut_asserteq(3, end - (dot + 1));
	ut_asserteq(0, *end);

	*msp = secs * 1000 + ms;

	return 0;
}

/* Test 'timer' measuring an interval */
static int cmd_test_timer_base(struct unit_test_state *uts)
{
	ulong ms;

	ut_assertok(run_command("timer start", 0));
	ut_assert_console_end();

	/*
	 * Advance the system time rather than waiting for it, so the test does
	 * not have to sleep. Only the time the test itself takes is unknown,
	 * hence the slack
	 */
	timer_test_add_offset(1500);
	ut_assertok(run_command("timer get", 0));
	ut_assertok(read_elapsed(uts, &ms));
	ut_assert_console_end();
	ut_assert(ms >= 1500 && ms < 1500 + SLACK_MS);

	/* the reference point stays put, so the total keeps growing */
	timer_test_add_offset(2250);
	ut_assertok(run_command("timer get", 0));
	ut_assertok(read_elapsed(uts, &ms));
	ut_assert_console_end();
	ut_assert(ms >= 3750 && ms < 3750 + SLACK_MS);

	/* a new start moves the reference point to now */
	ut_assertok(run_command("timer start", 0));
	ut_assertok(run_command("timer get", 0));
	ut_assertok(read_elapsed(uts, &ms));
	ut_assert_console_end();
	ut_assert(ms < SLACK_MS);

	return 0;
}
CMD_TEST(cmd_test_timer_base, UTF_CONSOLE);

/* Test that an interval of more than a minute is still shown in seconds */
static int cmd_test_timer_long(struct unit_test_state *uts)
{
	ulong ms;

	ut_assertok(run_command("timer start", 0));

	/* the seconds are not broken up into minutes */
	timer_test_add_offset(90500);
	ut_assertok(run_command("timer get", 0));
	ut_assertok(read_elapsed(uts, &ms));
	ut_assert_console_end();
	ut_assert(ms >= 90500 && ms < 90500 + SLACK_MS);

	return 0;
}
CMD_TEST(cmd_test_timer_long, UTF_CONSOLE);

/* Test an argument which the command does not know */
static int cmd_test_timer_unknown(struct unit_test_state *uts)
{
	ulong ms;

	ut_assertok(run_command("timer start", 0));
	timer_test_add_offset(1000);

	/*
	 * Anything other than 'start' and 'get' is quietly ignored: it neither
	 * prints the time nor moves the reference point
	 */
	ut_assertok(run_command("timer stop", 0));
	ut_assert_console_end();

	ut_assertok(run_command("timer get", 0));
	ut_assertok(read_elapsed(uts, &ms));
	ut_assert_console_end();
	ut_assert(ms >= 1000 && ms < 1000 + SLACK_MS);

	return 0;
}
CMD_TEST(cmd_test_timer_unknown, UTF_CONSOLE);

/* Test 'timer' with the wrong number of arguments */
static int cmd_test_timer_usage(struct unit_test_state *uts)
{
	/* the command needs exactly one argument */
	ut_asserteq(1, run_command("timer", 0));
	ut_assert_nextline("timer - access the system timer");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("timer start");
	ut_assert_nextlinen("timer get");
	ut_assert_console_end();

	ut_asserteq(1, run_command("timer get now", 0));
	ut_assert_nextline("timer - access the system timer");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("timer start");
	ut_assert_nextlinen("timer get");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_timer_usage, UTF_CONSOLE);
