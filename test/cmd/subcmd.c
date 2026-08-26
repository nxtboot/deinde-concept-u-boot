// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for sub-command dispatch
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * The cpu command is used because its sub-command table mixes the two cases:
 * 'list' may be repeated by pressing Enter, 'detail' may not
 */

/* Test that a dispatcher reports the repeatability of its sub-command */
static int cmd_test_subcmd_repeat(struct unit_test_state *uts)
{
	char *const list_args[] = { "cpu", "list", NULL };
	char *const detail_args[] = { "cpu", "detail", NULL };
	int repeatable;

	/*
	 * The command itself is marked repeatable, so without the dispatcher
	 * narrowing it, every sub-command would look repeatable
	 */
	repeatable = -1;
	ut_assertok(cmd_process(0, 2, list_args, &repeatable, NULL));
	ut_asserteq(1, repeatable);
	console_record_reset();

	/* 'detail' is not repeatable, and the dispatcher has to say so */
	repeatable = -1;
	ut_assertok(cmd_process(0, 2, detail_args, &repeatable, NULL));
	ut_asserteq(0, repeatable);
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_subcmd_repeat, UTF_CONSOLE);

/* Test that a repeat of a command whose sub-command cannot repeat does nothing */
static int cmd_test_subcmd_repeat_skip(struct unit_test_state *uts)
{
	char *const args[] = { "cpu", "detail", NULL };
	int repeatable;

	/*
	 * Run it once and throw away what it prints, which depends on the
	 * CPUs the board has
	 */
	cmd_process(0, 2, args, &repeatable, NULL);
	console_record_reset();

	/*
	 * Repeating it is refused before the sub-command runs, so nothing is
	 * printed at all
	 */
	ut_assertok(cmd_process(CMD_FLAG_REPEAT, 2, args, &repeatable, NULL));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_subcmd_repeat_skip, UTF_CONSOLE);
