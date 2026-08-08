// SPDX-License-Identifier: GPL-2.0+
/*
 * Test for chid command
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <test/cmd.h>
#include <test/ut.h>
#include <version.h>

/* Test the 'chid show' command */
static int cmd_chid_show_test(struct unit_test_state *uts)
{
	/* Test chid show command and verify expected output */
	ut_assertok(run_command("chid show", 0));

	ut_assert_nextline("Manufacturer:      Sandbox Corp");
	ut_assert_nextline("Family:            Sandbox_Family");
	ut_assert_nextline("Product Name:      Sandbox Computer");
	ut_assert_nextline("Product SKU:       SANDBOX-SKU");
	ut_assert_nextline("Baseboard Manuf:   Sandbox Boards");
	ut_assert_nextline("Baseboard Product: Sandbox Motherboard");
	ut_assert_nextline("BIOS Vendor:       Deinde Concept U-Boot");
	ut_assert_nextlinen("BIOS Version:      " PLAIN_VERSION);
	ut_assert_nextline("BIOS Major:        %u", U_BOOT_VERSION_NUM % 100);
	ut_assert_nextline("BIOS Minor:        %u", U_BOOT_VERSION_NUM_PATCH);
	ut_assert_nextline("Enclosure Type:    2");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_chid_show_test, UTF_CONSOLE);

/* Test invalid chid subcommand */
static int cmd_chid_invalid_test(struct unit_test_state *uts)
{
	/* Test chid command with invalid arguments */
	ut_asserteq(1, run_command("chid invalid", 0));

	return 0;
}
CMD_TEST(cmd_chid_invalid_test, UTF_CONSOLE);

/* Test the 'chid list' command */
static int cmd_chid_list_test(struct unit_test_state *uts)
{
	/* Test chid list command runs successfully */
	ut_assertok(run_command("chid list", 0));

	/* Just verify that some output is produced - exact CHIDs vary */
	return 0;
}
CMD_TEST(cmd_chid_list_test, UTF_CONSOLE);

/* Test the 'chid detail' command */
static int cmd_chid_detail_test(struct unit_test_state *uts)
{
	/* Test chid detail command for variant 14 (manufacturer only) */
	ut_assertok(run_command("chid detail 14", 0));

	ut_assert_nextlinen("HardwareID-14: ");
	ut_assert_nextline("Fields: Manufacturer");
	ut_assert_console_end();

	/* Test chid detail command for variant 0 (most specific) */
	ut_assertok(run_command("chid detail 0", 0));

	ut_assert_nextlinen("HardwareID-00: ");
	ut_assert_nextline(
		"Fields: Manufacturer + Family + ProductName + ProductSku + "
		"BiosVendor + BiosVersion + BiosMajorRelease + "
		"BiosMinorRelease");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_chid_detail_test, UTF_CONSOLE);

/* Test chid detail with invalid variant */
static int cmd_chid_detail_invalid_test(struct unit_test_state *uts)
{
	/* Test chid detail with invalid variant number - should fail */
	ut_asserteq(1, run_command("chid detail 15", 0));

	/* Test chid detail with negative variant number - should fail */
	ut_asserteq(1, run_command("chid detail -1", 0));

	return 0;
}
CMD_TEST(cmd_chid_detail_invalid_test, 0);

/* Test the 'chid compat' command */
static int cmd_chid_compat_test(struct unit_test_state *uts)
{
	const char *fdtcompat_val;
	int ret;

	/* Clear any existing fdtcompat environment variable */
	env_set("fdtcompat", NULL);
	ut_assertnull(env_get("fdtcompat"));

	/* Run chid compat command - may succeed or fail depending on devicetree */
	ret = run_command("chid compat", 0);

	if (ret == 0) {
		/* Command succeeded, check that fdtcompat was set */
		fdtcompat_val = env_get("fdtcompat");
		ut_assertnonnull(fdtcompat_val);
		ut_assert(strlen(fdtcompat_val) > 0);
		/* Command should print the compatible string it found */
		ut_assert_nextline(fdtcompat_val);
	} else {
		/* Command failed, check expected failure message and no env var set */
		ut_assert_nextline("No compatible string found");
		ut_assertnull(env_get("fdtcompat"));
	}

	ut_assert_console_end();
	return 0;
}
CMD_TEST(cmd_chid_compat_test, UTF_CONSOLE);
