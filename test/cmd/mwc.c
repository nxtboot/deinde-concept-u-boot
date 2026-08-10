// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mwc command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define MWC_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define MWC_SIZE	0x10

/* Test 'mwc' writing to memory until Ctrl-C is pressed */
static int cmd_test_mwc_base(struct unit_test_state *uts)
{
	int prev;
	u32 *buf;

	buf = map_sysmem(MWC_ADDR, MWC_SIZE);
	memset(buf, '\0', MWC_SIZE);

	/*
	 * The command runs until Ctrl-C is pressed, so queue one up before
	 * starting it. Sandbox turns off Ctrl-C checking, so enable it for the
	 * duration of the command
	 */
	console_in_puts("\x03");
	prev = disable_ctrlc(0);
	ut_assertok(run_commandf("mwc %lx 12345678 0", (ulong)MWC_ADDR));
	disable_ctrlc(prev);

	ut_assert_nextline("Abort");
	ut_assert_console_end();

	/* only a single value is written, however many times the loop runs */
	ut_asserteq(0x12345678, buf[0]);
	ut_asserteq(0, buf[1]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_mwc_base, UTF_CONSOLE);

/* Test 'mwc.b' writing a single byte */
static int cmd_test_mwc_byte(struct unit_test_state *uts)
{
	int prev;
	u8 *buf;

	buf = map_sysmem(MWC_ADDR, MWC_SIZE);
	memset(buf, '\0', MWC_SIZE);

	console_in_puts("\x03");
	prev = disable_ctrlc(0);
	ut_assertok(run_commandf("mwc.b %lx 55 0", (ulong)MWC_ADDR));
	disable_ctrlc(prev);

	ut_assert_nextline("Abort");
	ut_assert_console_end();

	/* the size suffix limits the write to one byte */
	ut_asserteq(0x55, buf[0]);
	ut_asserteq(0, buf[1]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_mwc_byte, UTF_CONSOLE);

/* Test 'mwc' with a bad command line */
static int cmd_test_mwc_usage(struct unit_test_state *uts)
{
	/* all three arguments are required */
	ut_asserteq(1, run_commandf("mwc %lx 55", (ulong)MWC_ADDR));
	ut_assert_nextline("mwc - memory write cyclic");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("mwc [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mwc_usage, UTF_CONSOLE);
