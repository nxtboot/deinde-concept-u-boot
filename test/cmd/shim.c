// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the shim command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <efi_loader.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Test reading and writing shim's verbose-mode variable */
static int cmd_test_shim_debug(struct unit_test_state *uts)
{
	/* keep what starting EFI prints out of the recorded output */
	ut_asserteq(EFI_SUCCESS, efi_init_obj_list());
	console_record_reset_enable();

	ut_assertok(run_command("shim debug 0", 0));
	ut_assert_console_end();

	ut_assertok(run_command("shim debug", 0));
	ut_assert_nextline("0");
	ut_assert_console_end();

	ut_assertok(run_command("shim debug 1", 0));
	ut_assert_console_end();

	ut_assertok(run_command("shim debug", 0));
	ut_assert_nextline("1");
	ut_assert_console_end();

	ut_assertok(run_command("shim debug 0", 0));
	ut_assertok(run_command("shim debug", 0));
	ut_assert_nextline("0");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_shim_debug, UTF_CONSOLE);
