// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mm command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define MM_ADDR		(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define MM_SIZE		0x10

/* Test 'mm' writing values at increasing addresses */
static int cmd_test_mm_base(struct unit_test_state *uts)
{
	u32 *buf;

	buf = map_sysmem(MM_ADDR, MM_SIZE);
	memset(buf, '\0', MM_SIZE);

	/*
	 * Write two values, skip the third with an empty line, then finish with
	 * something which is not a hex value
	 */
	console_in_puts("1234\n5678\n\nq\n");
	ut_assertok(run_commandf("mm %lx", (ulong)MM_ADDR));
	ut_assert_nextline("%08lx: 00000000 ? 1234", (ulong)MM_ADDR);
	ut_assert_nextline("%08lx: 00000000 ? 5678", (ulong)MM_ADDR + 4);
	ut_assert_nextline("%08lx: 00000000 ? ", (ulong)MM_ADDR + 8);
	ut_assert_nextline("%08lx: 00000000 ? q", (ulong)MM_ADDR + 12);
	ut_assert_console_end();

	/* only the addresses which were given a value are changed */
	ut_asserteq(0x1234, buf[0]);
	ut_asserteq(0x5678, buf[1]);
	ut_asserteq(0, buf[2]);
	ut_asserteq(0, buf[3]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_mm_base, UTF_CONSOLE);

/* Test 'mm.b' with a byte size and stepping back with '-' */
static int cmd_test_mm_back(struct unit_test_state *uts)
{
	u8 *buf;

	buf = map_sysmem(MM_ADDR, MM_SIZE);
	memset(buf, '\0', MM_SIZE);

	/* correct the value written to the second byte */
	console_in_puts("11\n22\n-\n33\nq\n");
	ut_assertok(run_commandf("mm.b %lx", (ulong)MM_ADDR));
	ut_assert_nextline("%08lx: 00 ? 11", (ulong)MM_ADDR);
	ut_assert_nextline("%08lx: 00 ? 22", (ulong)MM_ADDR + 1);
	ut_assert_nextline("%08lx: 00 ? -", (ulong)MM_ADDR + 2);
	ut_assert_nextline("%08lx: 22 ? 33", (ulong)MM_ADDR + 1);
	ut_assert_nextline("%08lx: 00 ? q", (ulong)MM_ADDR + 2);
	ut_assert_console_end();

	ut_asserteq(0x11, buf[0]);
	ut_asserteq(0x33, buf[1]);
	ut_asserteq(0, buf[2]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_mm_back, UTF_CONSOLE);

/* Test 'mm' with a bad command line */
static int cmd_test_mm_usage(struct unit_test_state *uts)
{
	/* the address is required */
	ut_asserteq(1, run_command("mm", 0));
	ut_assert_nextline("mm - memory modify (auto-incrementing address)");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("mm [.b, .w, .l");
	ut_assert_console_end();

	/* only one address is accepted */
	ut_asserteq(1, run_commandf("mm %lx %lx", (ulong)MM_ADDR,
				    (ulong)MM_ADDR));
	ut_assert_nextline("mm - memory modify (auto-incrementing address)");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("mm [.b, .w, .l");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mm_usage, UTF_CONSOLE);
