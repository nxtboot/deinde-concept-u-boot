// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the bootcount command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <bootcount.h>
#include <dm.h>
#include <test/cmd.h>
#include <test/ut.h>

/**
 * get_dev() - Find the device the command reads and writes
 *
 * bootcount_load() takes the device named by '/chosen/u-boot,bootcount-device'
 * and falls back to the first one in the uclass. The sandbox devicetree names
 * none, so the fallback is what the command uses.
 *
 * @uts: Test state
 * @devp: Returns the device
 * Return: 0 if OK, other value on error
 */
static int get_dev(struct unit_test_state *uts, struct udevice **devp)
{
	ut_assertok(uclass_get_device(UCLASS_BOOTCOUNT, 0, devp));

	return 0;
}

/* Test printing the counter and setting it back to zero */
static int cmd_test_bootcount_base(struct unit_test_state *uts)
{
	struct udevice *dev;
	u32 val;

	ut_assertok(get_dev(uts, &dev));
	ut_assertok(dm_bootcount_set(dev, 7));

	ut_assertok(run_command("bootcount print", 0));
	ut_assert_nextline("7");
	ut_assert_console_end();

	/* reset says nothing, so the only sign of it is the next print */
	ut_assertok(run_command("bootcount reset", 0));
	ut_assert_console_end();

	ut_assertok(run_command("bootcount print", 0));
	ut_assert_nextline("0");
	ut_assert_console_end();

	/* the driver holds the counter, not the command */
	ut_assertok(dm_bootcount_get(dev, &val));
	ut_asserteq(0, val);

	return 0;
}
CMD_TEST(cmd_test_bootcount_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/*
 * Test that a counter which cannot be read reports zero rather than an error.
 * The RTC-backed driver keeps a magic byte alongside the count and refuses a
 * value which does not carry it, which is how a counter that has never been
 * written looks
 */
static int cmd_test_bootcount_unset(struct unit_test_state *uts)
{
	struct udevice *dev;
	u32 val;

	ut_assertok(get_dev(uts, &dev));
	ut_asserteq(-EIO, dm_bootcount_get(dev, &val));

	ut_assertok(run_command("bootcount print", 0));
	ut_assert_nextline("0");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bootcount_unset, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test the arguments the command refuses */
static int cmd_test_bootcount_bad(struct unit_test_state *uts)
{
	/* a sub-command is required */
	ut_asserteq(1, run_command("bootcount", 0));
	ut_assert_nextline("bootcount - bootcount");
	ut_assert_skip_to_line("reset - reset the bootcounter");
	ut_assert_console_end();

	/* and it has to be one of the two */
	ut_asserteq(1, run_command("bootcount bogus", 0));
	ut_assert_nextline("bootcount - bootcount");
	ut_assert_skip_to_line("reset - reset the bootcounter");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bootcount_bad, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);
