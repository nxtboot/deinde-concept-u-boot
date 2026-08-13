// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the random command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define RAND_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define RAND_SIZE	0x20

/* Number of bytes filled by most of these tests */
#define RAND_LEN	0x10

/**
 * has_data() - Check whether a region holds anything other than zeroes
 *
 * @buf: Region to check
 * @len: Number of bytes to check
 * Return: true if any byte is non-zero, false if the region is all zeroes
 */
static bool has_data(const u8 *buf, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (buf[i])
			return true;
	}

	return false;
}

/* Test 'random' filling a region of memory */
static int cmd_test_random_base(struct unit_test_state *uts)
{
	u8 *buf;

	buf = map_sysmem(RAND_ADDR, RAND_SIZE);
	memset(buf, '\0', RAND_SIZE);

	ut_assertok(run_commandf("random %lx %x 1234", (ulong)RAND_ADDR,
				 RAND_LEN));

	/* the length is given in hex but the message counts in decimal */
	ut_assert_nextline("16 bytes filled with random data");
	ut_assert_console_end();

	/* the region is filled and nothing beyond it is touched */
	ut_assert(has_data(buf, RAND_LEN));
	ut_assert(!has_data(buf + RAND_LEN, RAND_SIZE - RAND_LEN));
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_random_base, UTF_CONSOLE);

/* Test that the seed decides the data */
static int cmd_test_random_seed(struct unit_test_state *uts)
{
	u8 first[RAND_LEN];
	u8 *buf;

	buf = map_sysmem(RAND_ADDR, RAND_SIZE);

	ut_assertok(run_commandf("random %lx %x 1234", (ulong)RAND_ADDR,
				 RAND_LEN));
	ut_assert_nextline("16 bytes filled with random data");
	memcpy(first, buf, RAND_LEN);

	/* the same seed produces the same data, so a fill can be repeated */
	memset(buf, '\0', RAND_LEN);
	ut_assertok(run_commandf("random %lx %x 1234", (ulong)RAND_ADDR,
				 RAND_LEN));
	ut_assert_nextline("16 bytes filled with random data");
	ut_asserteq_mem(first, buf, RAND_LEN);

	/* a different seed produces different data */
	ut_assertok(run_commandf("random %lx %x 4321", (ulong)RAND_ADDR,
				 RAND_LEN));
	ut_assert_nextline("16 bytes filled with random data");
	ut_assert(memcmp(first, buf, RAND_LEN));
	ut_assert_console_end();
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_random_seed, UTF_CONSOLE);

/* Test that a seed of 0 is replaced by a fixed one */
static int cmd_test_random_zero_seed(struct unit_test_state *uts)
{
	u8 expect[RAND_LEN];
	u8 *buf;

	buf = map_sysmem(RAND_ADDR, RAND_SIZE);

	ut_assertok(run_commandf("random %lx %x deadbeef", (ulong)RAND_ADDR,
				 RAND_LEN));
	ut_assert_nextline("16 bytes filled with random data");
	memcpy(expect, buf, RAND_LEN);

	/* a seed of 0 would leave the generator stuck, so it is swapped out */
	memset(buf, '\0', RAND_LEN);
	ut_assertok(run_commandf("random %lx %x 0", (ulong)RAND_ADDR,
				 RAND_LEN));
	ut_assert_nextline("The seed cannot be 0. Using 0xDEADBEEF.");
	ut_assert_nextline("16 bytes filled with random data");
	ut_asserteq_mem(expect, buf, RAND_LEN);
	ut_assert_console_end();
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_random_zero_seed, UTF_CONSOLE);

/* Test a length which is not a multiple of the word size */
static int cmd_test_random_len(struct unit_test_state *uts)
{
	u8 *buf;

	buf = map_sysmem(RAND_ADDR, RAND_SIZE);
	memset(buf, '\0', RAND_SIZE);

	/*
	 * The data is produced a word at a time, with any odd bytes at the end
	 * done singly, so check that a length which is not a multiple of four
	 * fills exactly that many bytes. The seed is fixed, so the two bytes
	 * beyond the last whole word are known to be non-zero
	 */
	ut_assertok(run_commandf("random %lx 12 1234", (ulong)RAND_ADDR));
	ut_assert_nextline("18 bytes filled with random data");
	ut_assert_console_end();

	ut_assert(has_data(buf, 0x12));
	ut_assert(has_data(buf + 0x10, 2));
	ut_assert(!has_data(buf + 0x12, RAND_SIZE - 0x12));
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_random_len, UTF_CONSOLE);

/* Test that the base offset is not applied to the address */
static int cmd_test_random_offset(struct unit_test_state *uts)
{
	int ret_set, ret_rand, ret_clr;
	u8 *buf;

	buf = map_sysmem(RAND_ADDR, RAND_SIZE);
	memset(buf, '\0', RAND_SIZE);

	/*
	 * Set an offset the size of the fill, do the fill and put the offset
	 * back. Nothing is checked until the offset is restored, since leaving
	 * it set would upset every later test which uses a memory command. The
	 * offset must not move the fill outside the region checked here, since
	 * a stray write to memory the board is using is fatal
	 */
	ret_set = run_commandf("base %x", RAND_LEN);
	ret_rand = run_commandf("random %lx %x 1234", (ulong)RAND_ADDR,
				RAND_LEN);
	ret_clr = run_command("base 0", 0);
	console_record_reset();

	ut_assertok(ret_set);
	ut_assertok(ret_rand);
	ut_assertok(ret_clr);

	/* the fill went to the address given, not to the offset plus it */
	ut_assert(has_data(buf, RAND_LEN));
	ut_assert(!has_data(buf + RAND_LEN, RAND_SIZE - RAND_LEN));
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_random_offset, UTF_CONSOLE);

/* Test 'random' with a bad command line */
static int cmd_test_random_usage(struct unit_test_state *uts)
{
	/* a length is needed as well as an address */
	ut_asserteq(1, run_commandf("random %lx", (ulong)RAND_ADDR));
	ut_assert_nextline("random - fill memory with random pattern");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("random <addr> <len>");
	ut_assert_nextlinen("   - Fill 'len' bytes");
	ut_assert_nextline_empty();

	/* the seed is the last argument the command takes */
	ut_asserteq(1, run_commandf("random %lx %x 1234 5678", (ulong)RAND_ADDR,
				    RAND_LEN));
	ut_assert_nextline("random - fill memory with random pattern");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("random <addr> <len>");
	ut_assert_nextlinen("   - Fill 'len' bytes");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_random_usage, UTF_CONSOLE);
