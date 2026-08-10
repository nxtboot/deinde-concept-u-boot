// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the editenv command
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Test 'editenv' creating and extending a variable */
static int cmd_test_editenv_base(struct unit_test_state *uts)
{
	/* an unset variable starts out empty, so the typing is all there is */
	ut_assertok(env_set("ut_edit", NULL));
	console_in_puts("wilma\n");
	ut_assertok(run_command("editenv ut_edit", 0));
	ut_assert_nextline("edit: wilma");
	ut_assert_console_end();
	ut_asserteq_str("wilma", env_get("ut_edit"));

	/* the old value is presented for editing, so this appends to it */
	console_in_puts(" flintstone\n");
	ut_assertok(run_command("editenv ut_edit", 0));
	ut_assert_nextline("edit: wilma flintstone");
	ut_assert_console_end();
	ut_asserteq_str("wilma flintstone", env_get("ut_edit"));

	/* pressing Enter on its own leaves the value alone */
	console_in_puts("\n");
	ut_assertok(run_command("editenv ut_edit", 0));
	ut_assert_nextline("edit: wilma flintstone");
	ut_assert_console_end();
	ut_asserteq_str("wilma flintstone", env_get("ut_edit"));

	return 0;
}
CMD_TEST(cmd_test_editenv_base, UTF_CONSOLE);

/* Test 'editenv' deleting a variable and rejecting a bad command line */
static int cmd_test_editenv_delete(struct unit_test_state *uts)
{
	ut_assertok(env_set("ut_edit", "barney"));

	/* Ctrl-C abandons the edit, leaving the old value in place */
	console_in_puts("stone\x03");
	ut_asserteq(1, run_command("editenv ut_edit", 0));
	console_record_reset();
	ut_asserteq_str("barney", env_get("ut_edit"));

	/* erasing every character deletes the variable */
	console_in_puts("\b\b\b\b\b\b\n");
	ut_assertok(run_command("editenv ut_edit", 0));
	console_record_reset();
	ut_assertnull(env_get("ut_edit"));

	/* the name is required */
	ut_asserteq(1, run_command("editenv", 0));
	ut_assert_nextlinen("editenv - edit environment variable");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Usage:");
	ut_assert_skip_to_linen("    - edit environment variable");
	ut_assert_console_end();

	/*
	 * An unknown option is rejected before anything is read, so there is
	 * no 'edit:' prompt and no attempt to edit a variable of that name.
	 */
	ut_asserteq(1, run_command("editenv -x", 0));
	ut_assert_nextlinen("editenv - edit environment variable");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Usage:");
	ut_assert_skip_to_linen("    - edit environment variable");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_editenv_delete, UTF_CONSOLE);
