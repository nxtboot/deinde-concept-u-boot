// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the tpmtest command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * As for the tpm command, these tests avoid UTF_DM: do_tpm() reads the
 * currently selected TPM before it dispatches, so a static left pointing into
 * a tree which has gone cannot be recovered from
 */
#define TPMTEST_FLAGS	UTF_CONSOLE

/* the last line of the help text, which ends the usage message */
#define TPMTEST_USAGE_LAST	"\twrite_limit"

/*
 * Select the emulated TPMv1.x device and skip the two lines of debugging
 * output the command prints before every subtest
 */
static int start_subtest(struct unit_test_state *uts, const char *name)
{
	ut_assertok(run_command("tpm device 1", 0));
	ut_assert_console_end();

	ut_assertok(run_commandf("tpmtest %s", name));
	ut_assert_nextline("argc = 2, argv =  tpmtest %s", name);
	ut_assert_nextline("------");

	return 0;
}

/* Test a subtest which the emulation can complete */
static int cmd_test_tpmtest_base(struct unit_test_state *uts)
{
	ut_assertok(start_subtest(uts, "early_extend"));
	ut_assert_nextline("Testing earlyextend ...done");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpmtest_base, TPMTEST_FLAGS);

/* Test the subtests which read the permanent flags */
static int cmd_test_tpmtest_enable(struct unit_test_state *uts)
{
	ut_assertok(start_subtest(uts, "enable"));

	/* the 'Get flags' lines come from the emulation, not the subtest */
	ut_assert_nextline("Testing enable ...");
	ut_assert_nextline("Get flags index 0x108");
	ut_assert_nextline("\tdisable is 0, deactivated is 0");
	ut_assert_nextline("Get flags index 0x108");
	ut_assert_nextline("\tdisable is 0, deactivated is 0");
	ut_assert_nextline("\tdone");
	ut_assert_console_end();

	ut_assertok(start_subtest(uts, "startup"));
	ut_assert_nextline("Testing startup ...");
	ut_assert_nextline("Get flags index 0x108");
	ut_assert_nextline("\texecuting SelfTestFull");
	ut_assert_nextline("Get flags index 0x108");
	ut_assert_nextline("\tdone");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpmtest_enable, TPMTEST_FLAGS);

/* Test 'tpmtest timer', which reports the board timer rather than the TPM */
static int cmd_test_tpmtest_timer(struct unit_test_state *uts)
{
	ut_assertok(start_subtest(uts, "timer"));
	ut_assert_nextlinen("get_timer(0) = ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpmtest_timer, TPMTEST_FLAGS);

/*
 * Test a subtest which fails. The emulation has only the spaces a Chromium OS
 * device uses, so anything reaching index 0xda70 stops there
 */
static int cmd_test_tpmtest_fail(struct unit_test_state *uts)
{
	ut_assertok(run_command("tpm device 1", 0));
	ut_assert_console_end();

	ut_asserteq(1, run_command("tpmtest early_nvram", 0));
	ut_assert_nextline("argc = 2, argv =  tpmtest early_nvram");
	ut_assert_nextline("------");
	ut_assert_nextline("Testing earlynvram ...Invalid nv index 0xda70");
	ut_assert_nextlinen("TEST FAILED: line ");
	ut_assert_console_end();

	/*
	 * The failure must be reported as a plain command failure. A raw TPM
	 * error code is below -1, which hush reads as a request to exit, so
	 * the rest of the command line would be dropped
	 */
	ut_assertok(run_command("echo after", 0));
	ut_assert_nextline("after");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpmtest_fail, TPMTEST_FLAGS);

/* Test a name which is not a subtest */
static int cmd_test_tpmtest_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("tpmtest bogus", 0));
	ut_assert_nextline("argc = 2, argv =  tpmtest bogus");
	ut_assert_nextline("------");
	ut_assert_nextline("tpmtest - TPM tests");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(TPMTEST_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* with no subtest at all the same list comes out */
	ut_asserteq(1, run_command("tpmtest", 0));
	ut_assert_nextline("argc = 1, argv =  tpmtest");
	ut_assert_nextline("------");
	ut_assert_skip_to_line(TPMTEST_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpmtest_usage, TPMTEST_FLAGS);
