// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the showvar command
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Test 'showvar' with local variables which exist */
static int cmd_test_showvar(struct unit_test_state *uts)
{
	/* a local variable is created by a plain assignment */
	ut_assertok(run_command("fred=42", 0));
	ut_assert_console_end();
	ut_assertok(run_command("wilma=hello", 0));
	ut_assert_console_end();

	/* a single name shows just that variable */
	ut_assertok(run_command("showvar fred", 0));
	ut_assert_nextline("fred=42");
	ut_assert_console_end();

	/* several names are shown in the order they are given */
	ut_assertok(run_command("showvar wilma fred", 0));
	ut_assert_nextline("wilma=hello");
	ut_assert_nextline("fred=42");
	ut_assert_console_end();

	/*
	 * With no arguments every variable is shown. Other tests may leave
	 * variables behind, so just check that ours appear, in the order they
	 * were created.
	 */
	ut_assertok(run_command("showvar", 0));
	ut_assert_skip_to_line("fred=42");
	ut_assert_skip_to_line("wilma=hello");
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_showvar, UTF_CONSOLE);

/* Test 'showvar' with variables which do not exist */
static int cmd_test_showvar_missing(struct unit_test_state *uts)
{
	ut_assertok(run_command("barney=stone", 0));
	ut_assert_console_end();

	/* an unknown name reports an error */
	ut_asserteq(1, run_command("showvar nosuchvar", 0));
	ut_assert_nextline("## Error: \"nosuchvar\" not defined");
	ut_assert_console_end();

	/* a known name is still shown when a later name is unknown */
	ut_asserteq(1, run_command("showvar barney nosuchvar", 0));
	ut_assert_nextline("barney=stone");
	ut_assert_nextline("## Error: \"nosuchvar\" not defined");
	ut_assert_console_end();

	/* an environment variable is not a local variable */
	ut_assertok(run_command("setenv betty rubble", 0));
	ut_assert_console_end();
	ut_asserteq(1, run_command("showvar betty", 0));
	ut_assert_nextline("## Error: \"betty\" not defined");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_showvar_missing, UTF_CONSOLE);
