// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the date command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <dm.h>
#include <rtc.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Date used by most of these tests, and the line the command prints for it */
#define SET_DATE	"122512002026.30"
#define SET_DATE_OUT	"Date: 2026-12-25 (Friday)    Time: 12:00:30"

/**
 * save_time() - Read the current time so it can be put back afterwards
 *
 * Setting the clock changes sandbox state which outlives the test, so every
 * test here restores what it found
 *
 * @uts: Test state
 * @devp: Returns the RTC device used by the date command
 * @timep: Returns the time read from it
 * Return: 0 if OK, -ve on error
 */
static int save_time(struct unit_test_state *uts, struct udevice **devp,
		     struct rtc_time *timep)
{
	ut_assertok(uclass_get_device(UCLASS_RTC, 0, devp));
	ut_assertok(dm_rtc_get(*devp, timep));

	return 0;
}

/* Test 'date' showing and setting the time */
static int cmd_test_date_base(struct unit_test_state *uts)
{
	struct rtc_time old;
	struct udevice *dev;

	ut_assertok(save_time(uts, &dev, &old));

	/* the date is shown in full, with the weekday worked out from it */
	ut_assertok(run_command("date " SET_DATE, 0));
	ut_assert_nextline(SET_DATE_OUT);
	ut_assert_console_end();

	/* the clock keeps what it was given, so it reads back the same */
	ut_assertok(run_command("date", 0));
	ut_assert_nextline(SET_DATE_OUT);
	ut_assert_console_end();

	ut_assertok(dm_rtc_set(dev, &old));

	return 0;
}
CMD_TEST(cmd_test_date_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test the parts of the date which can be left out */
static int cmd_test_date_short(struct unit_test_state *uts)
{
	struct rtc_time old;
	struct udevice *dev;

	ut_assertok(save_time(uts, &dev, &old));

	ut_assertok(run_command("date " SET_DATE, 0));
	ut_assert_nextline(SET_DATE_OUT);

	/* without the seconds they are set to 0, not left alone */
	ut_assertok(run_command("date 010203042005", 0));
	ut_assert_nextline("Date: 2005-01-02 (Sunday)    Time:  3:04:00");

	/* with only two year digits the century in the clock is kept */
	ut_assertok(run_command("date 0708091026", 0));
	ut_assert_nextline("Date: 2026-07-08 (Wednesday)    Time:  9:10:00");

	/* with no year at all the year in the clock is kept */
	ut_assertok(run_command("date 11121314", 0));
	ut_assert_nextline("Date: 2026-11-12 (Thursday)    Time: 13:14:00");
	ut_assert_console_end();

	ut_assertok(dm_rtc_set(dev, &old));

	return 0;
}
CMD_TEST(cmd_test_date_short, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test 'date reset' */
static int cmd_test_date_reset(struct unit_test_state *uts)
{
	struct rtc_time old;
	struct udevice *dev;

	ut_assertok(save_time(uts, &dev, &old));

	ut_assertok(run_command("date " SET_DATE, 0));
	ut_assert_nextline(SET_DATE_OUT);

	/* a reset puts the clock back to midnight at the start of 2000 */
	ut_assertok(run_command("date reset", 0));
	ut_assert_nextline("Reset RTC...");
	ut_assert_nextline("Date: 2000-01-01 (Saturday)    Time:  0:00:00");
	ut_assert_console_end();

	ut_assertok(dm_rtc_set(dev, &old));

	return 0;
}
CMD_TEST(cmd_test_date_reset, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test dates which the command refuses */
static int cmd_test_date_bad(struct unit_test_state *uts)
{
	struct rtc_time old;
	struct udevice *dev;

	ut_assertok(save_time(uts, &dev, &old));

	ut_assertok(run_command("date " SET_DATE, 0));
	ut_assert_nextline(SET_DATE_OUT);

	/* only lengths of 8, 10 and 12 digits are accepted */
	ut_asserteq(1, run_command("date 99", 0));
	ut_assert_nextline("## Bad date format");

	/* there is no month 13 */
	ut_asserteq(1, run_command("date 13011200", 0));
	ut_assert_nextline("## Bad date format");

	/* nor a 31st of November */
	ut_asserteq(1, run_command("date 11311200", 0));
	ut_assert_nextline("## Bad date format");

	/* the hour goes up to 23 and the minute to 59 */
	ut_asserteq(1, run_command("date 01012400", 0));
	ut_assert_nextline("## Bad date format");
	ut_asserteq(1, run_command("date 01011260", 0));
	ut_assert_nextline("## Bad date format");

	/* the seconds must be exactly two digits */
	ut_asserteq(1, run_command("date 01011200.1", 0));
	ut_assert_nextline("## Bad date format");
	ut_assert_console_end();

	/* none of that changed the clock */
	ut_assertok(run_command("date", 0));
	ut_assert_nextline(SET_DATE_OUT);
	ut_assert_console_end();

	ut_assertok(dm_rtc_set(dev, &old));

	return 0;
}
CMD_TEST(cmd_test_date_bad, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test 'date' with too many arguments */
static int cmd_test_date_usage(struct unit_test_state *uts)
{
	/* the command takes at most one argument */
	ut_asserteq(1, run_command("date reset now", 0));
	ut_assert_nextline("date - get/set/reset date & time");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("date [MMDDhhmm");
	ut_assert_nextline("date reset");
	ut_assert_nextlinen("  - without arguments");
	ut_assert_nextlinen("  - with numeric argument");
	ut_assert_nextlinen("  - with 'reset' argument");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_date_usage, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);
