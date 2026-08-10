// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mdc command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define MDC_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define MDC_SIZE	0x10

/* Test 'mdc' displaying memory until Ctrl-C is pressed */
static int cmd_test_mdc_base(struct unit_test_state *uts)
{
	int prev;
	u8 *buf;

	buf = map_sysmem(MDC_ADDR, MDC_SIZE);
	memset(buf, 0xaa, MDC_SIZE);

	/*
	 * The command runs until Ctrl-C is pressed, so queue some up before
	 * starting it. Sandbox turns off Ctrl-C checking, so enable it for the
	 * duration of the command.
	 *
	 * Two are needed since print_buffer() watches for Ctrl-C as well and
	 * eats the first one
	 */
	console_in_puts("\x03\x03");
	prev = disable_ctrlc(0);
	ut_assertok(run_commandf("mdc %lx 4 0", (ulong)MDC_ADDR));
	disable_ctrlc(prev);

	/* one display happens before the Ctrl-C is noticed */
	ut_assert_nextline("%08lx: aaaaaaaa aaaaaaaa aaaaaaaa aaaaaaaa  ................",
			   (ulong)MDC_ADDR);
	ut_assert_nextline("Abort");
	ut_assert_console_end();
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_mdc_base, UTF_CONSOLE);

/* Test 'mdc' with a bad command line */
static int cmd_test_mdc_usage(struct unit_test_state *uts)
{
	/* all three arguments are required */
	ut_asserteq(1, run_commandf("mdc %lx 4", (ulong)MDC_ADDR));
	ut_assert_nextline("mdc - memory display cyclic");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("mdc [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mdc_usage, UTF_CONSOLE);
