// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the nm command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define NM_ADDR		(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define NM_SIZE		0x10

/* Test 'nm' writing values at a constant address */
static int cmd_test_nm_base(struct unit_test_state *uts)
{
	u32 *buf;

	buf = map_sysmem(NM_ADDR, NM_SIZE);
	memset(buf, '\0', NM_SIZE);

	/* each value replaces the last, at the same address */
	console_in_puts("abcd\n1111\nq\n");
	ut_assertok(run_commandf("nm %lx", (ulong)NM_ADDR));
	ut_assert_nextline("%08lx: 00000000 ? abcd", (ulong)NM_ADDR);
	ut_assert_nextline("%08lx: 0000abcd ? 1111", (ulong)NM_ADDR);
	ut_assert_nextline("%08lx: 00001111 ? q", (ulong)NM_ADDR);
	ut_assert_console_end();

	/* the following address is left alone */
	ut_asserteq(0x1111, buf[0]);
	ut_asserteq(0, buf[1]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_nm_base, UTF_CONSOLE);

/* Test 'nm.w' with a word size and an empty line */
static int cmd_test_nm_word(struct unit_test_state *uts)
{
	u16 *buf;

	buf = map_sysmem(NM_ADDR, NM_SIZE);
	memset(buf, '\0', NM_SIZE);

	/* an empty line leaves the value alone */
	console_in_puts("beef\n\nq\n");
	ut_assertok(run_commandf("nm.w %lx", (ulong)NM_ADDR));
	ut_assert_nextline("%08lx: 0000 ? beef", (ulong)NM_ADDR);
	ut_assert_nextline("%08lx: beef ? ", (ulong)NM_ADDR);
	ut_assert_nextline("%08lx: beef ? q", (ulong)NM_ADDR);
	ut_assert_console_end();

	ut_asserteq(0xbeef, buf[0]);
	ut_asserteq(0, buf[1]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_nm_word, UTF_CONSOLE);

/* Test 'nm' with a bad command line */
static int cmd_test_nm_usage(struct unit_test_state *uts)
{
	/* the address is required */
	ut_asserteq(1, run_command("nm", 0));
	ut_assert_nextline("nm - memory modify (constant address)");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("nm [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nm_usage, UTF_CONSOLE);
