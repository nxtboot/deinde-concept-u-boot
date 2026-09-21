// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the nvme command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <blk.h>
#include <console.h>
#include <mapmem.h>
#include <dm.h>
#include <dm/uclass-internal.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the last line of the help text, which ends the usage message */
#define NVME_USAGE_LAST	"     `blk#' from memory address `addr'"

/**
 * get_blk() - Find the block device of the first namespace
 *
 * Sandbox emulates a controller, so these tests have a namespace to talk to.
 * A board with no NVMe at all has nothing to test here.
 *
 * @uts: Test state
 * @blkp: Returns the block device
 * Return: 0 if OK, -EAGAIN to skip the test if there is no controller
 */
static int get_blk(struct unit_test_state *uts, struct udevice **blkp)
{
	struct udevice *dev;

	if (uclass_find_first_device(UCLASS_NVME, &dev) || !dev)
		return -EAGAIN;
	ut_assertok(run_command("nvme scan", 0));
	console_record_reset();
	ut_assertok(blk_get_device(UCLASS_NVME, 0, blkp));

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
	struct udevice *blk;
	int ret;

	ret = get_blk(uts, &blk);
	if (ret)
		return ret;

	/* the listing names the namespace the emulator provides */
	ut_assertok(run_command("nvme info", 0));
	ut_assert_nextlinen("Device 0: Vendor:");
	ut_assert_nextline("            Type: Hard Disk");
	ut_assert_nextlinen("            Capacity:");
	ut_assert_console_end();

	/* the current device is that one, and says so by its own name */
	ut_assertok(run_command("nvme device", 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("nvme device 0: Vendor:");
	ut_assert_skip_to_linen("            Capacity:");
	ut_assert_console_end();

	/* the emulated namespace holds no partition table */
	ut_assertok(run_command("nvme part", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("no nvme partition table available");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nvme_base, UTF_CONSOLE);

/* Test the sub-commands when the device asked for is not there */
static int cmd_test_nvme_missing(struct unit_test_state *uts)
{
	struct udevice *blk;
	int ret;

	ret = get_blk(uts, &blk);
	if (ret)
		return ret;

	/* there is one namespace, so the second does not exist */
	ut_asserteq(1, run_command("nvme dev 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 1: unknown device");
	ut_assert_console_end();

	ut_asserteq(1, run_command("nvme part 1", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("nvme device 1 not available");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nvme_missing, UTF_CONSOLE);

/* Test that the detail sub-command reports what Identify returns */
static int cmd_test_nvme_detail(struct unit_test_state *uts)
{
	struct udevice *blk;
	int ret;

	ret = get_blk(uts, &blk);
	if (ret)
		return ret;

	ut_assertok(run_command("nvme detail", 0));
	ut_assert_nextline("Blk device 0: Optional Admin Command Support:");
	ut_assert_skip_to_line("Blk device 0: Metadata capabilities:");
	ut_assert_skip_to_line("\tAs part of an extended data LBA: No");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nvme_detail, UTF_CONSOLE);

/* Test moving data to and from the namespace */
static int cmd_test_nvme_rw(struct unit_test_state *uts)
{
	ulong addr = CONFIG_SYS_LOAD_ADDR + 0x1000;
	struct udevice *blk;
	u8 *buf;
	int ret, i;

	ret = get_blk(uts, &blk);
	if (ret)
		return ret;

	buf = map_sysmem(addr, 512);
	for (i = 0; i < 512; i++)
		buf[i] = i;

	ut_assertok(run_commandf("nvme write %lx 5 1", addr));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("nvme write: device 0 block # 5, count 1 ...");
	ut_assert_console_end();

	memset(buf, '\0', 512);
	ut_assertok(run_commandf("nvme read %lx 5 1", addr));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("nvme read: device 0 block # 5, count 1 ...");
	ut_assert_console_end();

	/* what came back is what went out */
	for (i = 0; i < 512; i++)
		ut_asserteq(i & 0xff, buf[i]);

	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_nvme_rw, UTF_CONSOLE);

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
