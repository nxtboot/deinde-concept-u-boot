// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the scsiboot command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <blk.h>
#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the load address the tests read into */
#define SCSIBOOT_ADDR	"1000"

/* the line the command prints once it has found the partition */
#define LOADING_LINE \
	"Loading from scsi device 0, partition 0: Name: Whole Disk  Type: U-Boot"

/**
 * scan_bus() - Scan the SCSI bus and check the emulated drive is readable
 *
 * The sandbox drive is backed by scsi.img, which test.py creates, so a run
 * without that file finds a drive of no size and has nothing to read.
 *
 * @uts: Test state
 * Return: 0 if the drive is ready, -EAGAIN to skip the test if it is not, 1 on
 * failure
 */
static int scan_bus(struct unit_test_state *uts)
{
	struct blk_desc *desc;

	ut_assertok(run_command("scsi scan", 0));
	console_record_reset();

	desc = blk_get_devnum_by_uclass_id(UCLASS_SCSI, 0);
	if (!desc || !desc->lba)
		return -EAGAIN;

	return 0;
}

/* Test loading from a drive which holds no image */
static int cmd_test_scsiboot_base(struct unit_test_state *uts)
{
	int ret;

	ret = scan_bus(uts);
	if (ret)
		return ret;

	/*
	 * Partition 0 is the whole drive, so the read succeeds whatever the
	 * partition table says, and the format check is what fails
	 */
	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR " 0:0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline(LOADING_LINE);
	ut_assert_nextline("** Unknown image type");
	ut_assert_console_end();

	/* with no device given the bootdevice environment variable is used */
	ut_assertok(run_command("setenv bootdevice 0:0", 0));
	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline(LOADING_LINE);
	ut_assert_nextline("** Unknown image type");
	ut_assert_console_end();
	ut_assertok(run_command("setenv bootdevice", 0));

	return 0;
}
CMD_TEST(cmd_test_scsiboot_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test naming a drive or a partition which is not there */
static int cmd_test_scsiboot_missing(struct unit_test_state *uts)
{
	int ret;

	ret = scan_bus(uts);
	if (ret)
		return ret;

	/* no device is selected by default, so there is nothing to boot */
	ut_asserteq(1, run_command("scsiboot", 0));
	ut_assert_nextline("** No device specified **");
	ut_assert_console_end();

	/* the same goes for an address with no device behind it */
	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR, 0));
	ut_assert_nextline("** No device specified **");
	ut_assert_console_end();

	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR " 99:1", 0));
	ut_assert_nextline("** Bad device specification scsi 99 **");
	ut_assert_console_end();

	/* the drive is there but the partition is not */
	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR " 0:9", 0));
	ut_assert_nextline("** Invalid partition 9 **");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_scsiboot_missing, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA |
	 UTF_SCAN_FDT);

/* Test the command lines which the command refuses */
static int cmd_test_scsiboot_usage(struct unit_test_state *uts)
{
	/* the command takes two arguments at most */
	ut_asserteq(1, run_command("scsiboot " SCSIBOOT_ADDR " 0:0 1", 0));
	ut_assert_nextline("scsiboot - boot from SCSI device");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("scsiboot loadAddr dev:part");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_scsiboot_usage, UTF_CONSOLE);
