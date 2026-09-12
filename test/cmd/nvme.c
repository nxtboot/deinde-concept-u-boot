// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the nvme command
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
#define NVME_USAGE_LAST	"     `blk#' from memory address `addr'"

/**
 * check_no_nvme() - Check that no NVMe controller is present
 *
 * These tests cover what the command does when there is no namespace to talk
 * to, which is all that sandbox offers, so a board with a controller of its
 * own must skip them.
 *
 * Return: 0 if there is no controller, -EAGAIN to skip the test if there is
 */
static int check_no_nvme(void)
{
	struct udevice *dev;

	if (!uclass_find_first_device(UCLASS_NVME, &dev) && dev)
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
	ut_assert_nextline("nvme - NVM Express sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(NVME_USAGE_LAST);
	ut_assert_console_end();

	return 0;
}

/* Test the sub-commands which report on the namespaces */
static int cmd_test_nvme_base(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_nvme();
	if (ret)
		return ret;

	/* a scan which finds nothing says nothing */
	ut_assertok(run_command("nvme scan", 0));
	ut_assert_console_end();

	/* with no namespace the listing is empty, yet the command succeeds */
	ut_assertok(run_command("nvme info", 0));
	ut_assert_console_end();

	/* the partition listing says so instead, and also succeeds */
	ut_assertok(run_command("nvme part", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("no nvme partition table available");
	ut_assert_console_end();

	/* asking for the current device fails, since there is none */
	ut_asserteq(1, run_command("nvme device", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("no nvme devices available");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nvme_base, UTF_CONSOLE);

/* Test the sub-commands which need a device that is not there */
static int cmd_test_nvme_missing(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_nvme();
	if (ret)
		return ret;

	ut_asserteq(1, run_command("nvme detail", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("nvme device 0 not available");
	ut_assert_console_end();

	ut_asserteq(1, run_command("nvme dev 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 0: unknown device");
	ut_assert_console_end();

	ut_asserteq(1, run_command("nvme part 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("nvme device 0 not available");
	ut_assert_console_end();

	/*
	 * A transfer announces itself before looking for the device, so the
	 * line it prints is left unfinished
	 */
	ut_asserteq(1, run_command("nvme read 1000 0 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("nvme read: device 0 block # 0, count 1 ...");
	ut_assert_console_end();

	ut_asserteq(1, run_command("nvme write 1000 0 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("nvme write: device 0 block # 0, count 1 ...");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nvme_missing, UTF_CONSOLE);

/* Test the command lines which the command refuses */
static int cmd_test_nvme_usage(struct unit_test_state *uts)
{
	/* a sub-command is needed */
	ut_asserteq(1, run_command("nvme", 0));
	ut_assertok(check_usage(uts));

	/* and it must be one the command knows */
	ut_asserteq(1, run_command("nvme bogus", 0));
	ut_assertok(check_usage(uts));

	/* neither scan nor detail takes an argument */
	ut_asserteq(1, run_command("nvme scan now", 0));
	ut_assertok(check_usage(uts));

	/* a transfer needs an address, a block number and a count */
	ut_asserteq(1, run_command("nvme read 1000 0", 0));
	ut_assertok(check_usage(uts));

	ut_asserteq(1, run_command("nvme write 1000 0", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_nvme_usage, UTF_CONSOLE);
