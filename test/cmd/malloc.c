// SPDX-License-Identifier: GPL-2.0+
/*
 * Test for 'malloc' command
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <malloc.h>
#include <mapmem.h>
#include <dm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Test 'malloc info' command */
static int cmd_test_malloc_info(struct unit_test_state *uts)
{
	struct malloc_info info;

	ut_assertok(malloc_get_info(&info));
	ut_assert(info.total_bytes >= CONFIG_SYS_MALLOC_LEN);
	ut_assert(info.in_use_bytes < info.total_bytes);
	ut_assert(info.malloc_count > 0);

	ut_assertok(run_command("malloc info", 0));
	ut_assert_nextlinen("total bytes   = ");
	ut_assert_nextlinen("in use bytes  = ");
	ut_assert_nextlinen("largest free  = ");
	ut_assert_nextlinen("malloc count  = ");
	ut_assert_nextlinen("free count    = ");
	ut_assert_nextlinen("realloc count = ");
	if (IS_ENABLED(CONFIG_MCHECK_HEAP_PROTECTION))
		ut_assert_nextlinen("mcheck count  = ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_malloc_info, UTF_CONSOLE);

/* Test 'malloc dump' command */
static int cmd_test_malloc_dump(struct unit_test_state *uts)
{
	/* this takes a long time to dump, with truetype enabled, so skip it */
	return -EAGAIN;

	ut_assertok(run_command("malloc dump", 0));
	ut_assert_nextlinen("Heap dump: ");
	ut_assert_nextline("%12s  %10s  %s", "Address", "Size", "Status");
	ut_assert_nextline("----------------------------------");
	ut_assert_nextline("%12lx  %10x  (chunk header)", mem_malloc_start, 0x10);
	ut_assert_skip_to_line("----------------------------------");
	ut_assert_nextlinen("Used: ");
	ut_assert_nextlinen("Free: ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_malloc_dump, UTF_CONSOLE);

/* Test 'malloc leak' command using the C API directly */
static int cmd_test_malloc_leak(struct unit_test_state *uts)
{
	struct malloc_leak_snap snap = {};
	ulong chunk_addr;
	size_t chunk_sz;
	void *ptr;
	int ret;

	/*
	 * If the mcheck registry ever overflowed, later allocations are not
	 * registered and leak reports cannot recover the caller string.
	 */
	ut_assert(!malloc_mcheck_overflow());

	/* Take a snapshot, then check with no leaks */
	ut_assertok(malloc_leak_check_start(&snap));
	ut_assert(snap.count > 0);
	ut_asserteq(0, malloc_leak_check_count(&snap));

	/* Allocate something and check it is detected */
	ptr = malloc(0x42);
	ut_assertnonnull(ptr);
	ut_asserteq(1, malloc_leak_check_count(&snap));

	/* Verify freeing clears the leak */
	free(ptr);
	ut_asserteq(0, malloc_leak_check_count(&snap));

	/* Re-allocate so end has something to print */
	ptr = malloc(0x42);
	ut_assertnonnull(ptr);

	/* End should print the leaked allocation and free the snapshot */
	ret = malloc_leak_check_end(&snap);
	ut_asserteq(1, ret);

	/*
	 * Check the output line shows the correct chunk address and chunk
	 * size. The chunk address is the malloc pointer minus the mcheck
	 * header. The caller name is only available with mcheck.
	 */
	chunk_addr = (ulong)ptr - malloc_mcheck_hdr_size();
	chunk_sz = malloc_chunk_size(ptr);
	if (IS_ENABLED(CONFIG_MCHECK_HEAP_PROTECTION))
		ut_assert_nextlinen("  %lx %zx %s:", chunk_addr, chunk_sz,
				    __func__);
	else
		ut_assert_nextlinen("  %lx %zx ", chunk_addr, chunk_sz);
	ut_assert_console_end();
	ut_assertnull(snap.addr);

	free(ptr);

	return 0;
}
CMD_TEST(cmd_test_malloc_leak, UTF_CONSOLE);

#if CONFIG_IS_ENABLED(MCHECK_LOG)
/* Test 'malloc log' command */
static int cmd_test_malloc_log(struct unit_test_state *uts)
{
	struct mlog_info info;
	void *ptr, *ptr2;
	int seq;

	ut_assertok(run_command("malloc log start", 0));
	ut_assert_nextline("Malloc logging started");
	ut_assert_console_end();

	/* Get current log position so we know our sequence numbers */
	ut_assertok(malloc_log_info(&info));
	seq = info.total_count;

	/* Do allocations with distinctive sizes we can search for */
	ptr = malloc(12345);
	ut_assertnonnull(ptr);
	ptr2 = realloc(ptr, 23456);
	ut_assertnonnull(ptr2);
	free(ptr2);

	ut_assertok(run_command("malloc log stop", 0));
	ut_assert_nextline("Malloc logging stopped");
	ut_assert_console_end();

	/* Dump the log and find our allocations by sequence number and size */
	ut_assertok(run_command("malloc log", 0));
	ut_assert_nextlinen("Malloc log: ");
	ut_assert_nextline("%4s  %-8s  %10s  %8s  %s",
			   "Seq", "Type", "Address", "Size", "Caller");
	ut_assert_nextline("----  --------  ----------  --------  ------");
	/* 12345 = 0x3039, 23456 = 0x5ba0 */
	ut_assert_skip_to_linen("%4d  alloc     %10lx      3039", seq,
				(ulong)map_to_sysmem(ptr));
	ut_assert_skip_to_linen("%4d  realloc   %10lx      5ba0", seq + 1,
				(ulong)map_to_sysmem(ptr2));
	ut_assert_skip_to_linen("%4d  free      %10lx         0", seq + 2,
				(ulong)map_to_sysmem(ptr2));
	console_record_reset_enable();	/* discard remaining output */

	return 0;
}
CMD_TEST(cmd_test_malloc_log, UTF_CONSOLE);
#endif /* MCHECK_LOG */
