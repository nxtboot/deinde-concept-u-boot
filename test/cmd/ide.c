// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the ide command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the last line of the help text, which ends the usage message */
#define IDE_USAGE_LAST	"    to/from memory address `addr'"

/**
 * check_no_ide() - Check that no IDE controller is present
 *
 * These tests cover what the command does when there is no drive to talk to,
 * which is all that sandbox offers, so a board with a controller of its own
 * must skip them.
 *
 * Return: 0 if there is no controller, -EAGAIN to skip the test if there is
 */
static int check_no_ide(void)
{
	struct udevice *dev;

	if (!uclass_find_first_device(UCLASS_IDE, &dev) && dev)
		return -EAGAIN;

	return 0;
}

/**
 * check_usage() - Check the usage message, which a bad command line produces
 *
 * @uts: Test state
 * Return: 0 if OK, 1 on failure
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("ide - IDE sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(IDE_USAGE_LAST);
	ut_assert_console_end();

	return 0;
}

/* Test the sub-commands which report on the drives */
static int cmd_test_ide_base(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_ide();
	if (ret)
		return ret;

	/* with no drive the listing is empty, yet the command succeeds */
	ut_assertok(run_command("ide info", 0));
	ut_assert_console_end();

	/* the partition listing says so instead, and also succeeds */
	ut_assertok(run_command("ide part", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("no ide partition table available");
	ut_assert_console_end();

	/* asking for the current device fails, since there is none */
	ut_asserteq(1, run_command("ide device", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("no ide devices available");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_ide_base, UTF_CONSOLE);

/* Test the sub-commands which name a device that is not there */
static int cmd_test_ide_missing(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_ide();
	if (ret)
		return ret;

	ut_asserteq(1, run_command("ide dev 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 0: unknown device");
	ut_assert_console_end();

	ut_asserteq(1, run_command("ide part 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("ide device 0 not available");
	ut_assert_console_end();

	/*
	 * A transfer announces itself before looking for the device, so the
	 * line it prints is left unfinished
	 */
	ut_asserteq(1, run_command("ide read 1000 0 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("ide read: device 0 block # 0, count 1 ...");
	ut_assert_console_end();

	ut_asserteq(1, run_command("ide write 1000 0 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("ide write: device 0 block # 0, count 1 ...");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_ide_missing, UTF_CONSOLE);

/* Test that a reset reports the controller which is not there */
static int cmd_test_ide_reset(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_ide();
	if (ret)
		return ret;

	ut_asserteq(1, run_command("ide reset", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Reset IDE: No IDE controller");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_ide_reset, UTF_CONSOLE);

/* Test the command lines which the command refuses */
static int cmd_test_ide_usage(struct unit_test_state *uts)
{
	/* a sub-command is needed */
	ut_asserteq(1, run_command("ide", 0));
	ut_assertok(check_usage(uts));

	/* and it must be one the command knows */
	ut_asserteq(1, run_command("ide bogus", 0));
	ut_assertok(check_usage(uts));

	/* 'dev' takes a device number, not a block */
	ut_asserteq(1, run_command("ide dev 0 1", 0));
	ut_assertok(check_usage(uts));

	/* a transfer needs an address, a block number and a count */
	ut_asserteq(1, run_command("ide read 1000 0", 0));
	ut_assertok(check_usage(uts));

	ut_asserteq(1, run_command("ide write 1000 0", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_ide_usage, UTF_CONSOLE);
