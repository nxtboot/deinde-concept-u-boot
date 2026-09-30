// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the AMD EPYC Turin bring-up commands (mca and smn), as run
 * on the Gigabyte MZ33-AR1
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

static int cmd_test_mca(struct unit_test_state *uts)
{
	ut_assertok(run_command("mca all", 0));
	ut_assert_nextline("32 banks");
	ut_assert_nextlinen("bank  0: ipid ");
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_mca, UTF_CONSOLE);

static int cmd_test_smn(struct unit_test_state *uts)
{
	/* the bus-0 root complex's I/O APIC, which U-Boot enables */
	ut_assertok(run_command("pci enum", 0));
	console_record_reset();
	ut_assertok(run_command("smn md 13d102f0", 0));
	ut_assert_nextline("13d102f0: febf0001");
	ut_assert_console_end();

	/* the same register through another root bus's index/data pair */
	ut_assertok(run_command("smn md 13d102f0 1 20", 0));
	ut_assert_nextline("13d102f0: febf0001");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_smn, UTF_CONSOLE);
