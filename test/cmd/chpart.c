// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the chpart command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <env.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Partition table covering the whole of the 4MB sandbox NAND device */
#define TEST_PARTS	"nand0:1m(boot),2m(kernel),-(rootfs)"

/* How the listing reports the last partition of that table as current */
#define ACTIVE_ROOTFS \
	"active partition: nand0,2 - (rootfs) 0x00100000 @ 0x00300000"

/**
 * setup_parts() - Give chpart a partition table to choose from
 *
 * mtdparts keeps its device list and the current partition in statics which
 * outlive a single test, so drop the existing table before installing this
 * one. That leaves partition 0 current, whatever an earlier test did.
 *
 * @uts: Test state
 * Return: 0 if OK, other value on error
 */
static int setup_parts(struct unit_test_state *uts)
{
	ut_assertok(env_set("mtdids", "nand0=nand0"));
	ut_assertok(env_set("partition", NULL));
	ut_assertok(env_set("mtdparts", NULL));
	ut_assertok(run_command("mtdparts delall", 0));
	ut_assertok(env_set("mtdparts", TEST_PARTS));
	ut_assertok(run_command("mtdparts", 0));
	console_record_reset();

	return 0;
}

/* Test selecting a partition */
static int cmd_test_chpart_base(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts));

	ut_asserteq_str("nand0,0", env_get("partition"));

	ut_assertok(run_command("chpart nand0,2", 0));
	ut_assert_nextline("partition changed to nand0,2");
	ut_assert_console_end();

	/* the choice is recorded in the environment */
	ut_asserteq_str("nand0,2", env_get("partition"));

	/* and shows up as the active partition in the listing */
	ut_assertok(run_command("mtdparts", 0));
	ut_assert_skip_to_line(ACTIVE_ROOTFS);
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_chpart_base, UTF_CONSOLE);

/* Test that a part-id is required */
static int cmd_test_chpart_noarg(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts));

	ut_asserteq(1, run_command("chpart", 0));
	ut_assert_nextline("no partition id specified");
	ut_assert_console_end();

	/* the current partition is unchanged */
	ut_asserteq_str("nand0,0", env_get("partition"));

	return 0;
}
CMD_TEST(cmd_test_chpart_noarg, UTF_CONSOLE);

/* Test a part-id which does not name an existing partition */
static int cmd_test_chpart_bad(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts));

	ut_asserteq(1, run_command("chpart nand0,9", 0));
	ut_assert_nextline("invalid partition number 9 for device nand0 (nand0)");
	ut_assert_nextline("no such partition");
	ut_assert_console_end();

	ut_asserteq(1, run_command("chpart nand5,0", 0));
	ut_assert_nextline("no such device nand5");
	ut_assert_console_end();

	ut_asserteq(1, run_command("chpart wibble", 0));
	ut_assert_nextline("incorrect device type in wibble");
	ut_assert_console_end();

	ut_asserteq_str("nand0,0", env_get("partition"));

	return 0;
}
CMD_TEST(cmd_test_chpart_bad, UTF_CONSOLE);

/*
 * Test that a missing mtdids says so. Sandbox has no CONFIG_MTDIDS_DEFAULT,
 * so there is nothing to fall back on
 */
static int cmd_test_chpart_noids(struct unit_test_state *uts)
{
	ut_assertok(setup_parts(uts));

	ut_assertok(env_set("mtdids", NULL));

	ut_asserteq(1, run_command("chpart nand0,1", 0));
	ut_assert_nextline("mtdids not defined, no default present");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_chpart_noids, UTF_CONSOLE);
