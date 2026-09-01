// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mtdparts command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <env.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Partition table covering the whole of the 4MB sandbox NAND device */
#define TEST_PARTS	"nand0:1m(boot),2m(kernel),-(rootfs)"

/* How the listing reports the first partition of that table as current */
#define ACTIVE_BOOT \
	"active partition: nand0,0 - (boot) 0x00100000 @ 0x00000000"

/**
 * setup_parts() - Point mtdparts at the sandbox NAND device
 *
 * The command keeps its device list, its copy of the environment and the
 * current partition in statics which outlive a single test, so drop the
 * existing table first. That way the table is parsed afresh and partition 0
 * is current, whatever an earlier test left behind.
 *
 * @uts: Test state
 * @parts: Value for the mtdparts environment variable
 * Return: 0 if OK, other value on error
 */
static int setup_parts(struct unit_test_state *uts, const char *parts)
{
	ut_assertok(env_set("mtdids", "nand0=nand0"));
	ut_assertok(env_set("partition", NULL));
	ut_assertok(env_set("mtdparts", NULL));
	ut_assertok(run_command("mtdparts delall", 0));
	ut_assertok(env_set("mtdparts", parts));
	console_record_reset();

	return 0;
}

/* Test listing the partition table */
static int cmd_test_mtdparts_base(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts, TEST_PARTS));

	ut_assertok(run_command("mtdparts", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("device nand0 <nand0>, # parts = 3");
	ut_assert_nextline(" #: name\t\tsize\t\toffset\t\tmask_flags");
	ut_assert_nextline(" 0: boot                0x00100000\t0x00000000\t0");
	ut_assert_nextline(" 1: kernel              0x00200000\t0x00100000\t0");
	ut_assert_nextline(" 2: rootfs              0x00100000\t0x00300000\t0");
	ut_assert_nextline_empty();
	ut_assert_nextline(ACTIVE_BOOT);
	ut_assert_nextline_empty();
	ut_assert_nextline("defaults:");
	ut_assert_nextline("mtdids  : none");
	ut_assert_nextline("mtdparts: none");
	ut_assert_console_end();

	/* the command records the current partition in the environment */
	ut_asserteq_str("nand0,0", env_get("partition"));

	return 0;
}
CMD_TEST(cmd_test_mtdparts_base, UTF_CONSOLE);

/*
 * Test that a read-only partition shows up in the mask_flags column. This
 * also uses the optional 'mtdparts=' prefix, so that a kernel command-line
 * fragment can be pasted in unchanged
 */
static int cmd_test_mtdparts_ro(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts, "mtdparts=nand0:1m(boot)ro,-(rest)"));

	ut_assertok(run_command("mtdparts", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("device nand0 <nand0>, # parts = 2");
	ut_assert_nextlinen(" #: name");
	ut_assert_nextline(" 0: boot                0x00100000\t0x00000000\t1");
	ut_assert_nextline(" 1: rest                0x00300000\t0x00100000\t0");
	ut_assert_skip_to_line("mtdparts: none");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtdparts_ro, UTF_CONSOLE);

/* Test adding and deleting partitions one at a time */
static int cmd_test_mtdparts_edit(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts, TEST_PARTS));

	ut_assertok(run_command("mtdparts", 0));
	console_record_reset();

	/* deleting the middle partition leaves a gap, shown as an offset */
	ut_assertok(run_command("mtdparts del nand0,1", 0));
	ut_assert_console_end();
	ut_asserteq_str("nand0:1m(boot),1m@3m(rootfs)", env_get("mtdparts"));

	/* an added partition follows the last one */
	ut_assertok(run_command("mtdparts delall", 0));
	ut_assertnull(env_get("mtdparts"));
	console_record_reset();

	ut_assertok(run_command("mtdparts add nand0 1m first", 0));
	console_record_reset();
	ut_assertok(run_command("mtdparts add nand0 2m second", 0));
	ut_assert_console_end();
	ut_asserteq_str("nand0:1m(first),2m(second)", env_get("mtdparts"));

	ut_assertok(run_command("mtdparts", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("device nand0 <nand0>, # parts = 2");
	ut_assert_nextlinen(" #: name");
	ut_assert_nextline(" 0: first               0x00100000\t0x00000000\t0");
	ut_assert_nextline(" 1: second              0x00200000\t0x00100000\t0");
	ut_assert_skip_to_line("mtdparts: none");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtdparts_edit, UTF_CONSOLE);

/* Test deleting a partition which does not exist */
static int cmd_test_mtdparts_nopart(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts, TEST_PARTS));

	ut_assertok(run_command("mtdparts", 0));
	console_record_reset();

	ut_asserteq(1, run_command("mtdparts del nand0,7", 0));
	ut_assert_nextline("invalid partition number 7 for device nand0 (nand0)");
	ut_assert_nextline("no such partition");
	ut_assert_nextline("partition nand0,7 not found");
	ut_assert_console_end();

	/* the table is untouched */
	ut_asserteq_str(TEST_PARTS, env_get("mtdparts"));

	return 0;
}
CMD_TEST(cmd_test_mtdparts_nopart, UTF_CONSOLE);

/*
 * Test that a missing mtdids says so. Sandbox has no CONFIG_MTDIDS_DEFAULT,
 * so there is nothing to fall back on
 */
static int cmd_test_mtdparts_noids(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts, TEST_PARTS));

	ut_assertok(env_set("mtdids", NULL));

	ut_asserteq(1, run_command("mtdparts", 0));
	ut_assert_nextline("mtdids not defined, no default present");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtdparts_noids, UTF_CONSOLE);
