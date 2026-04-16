// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for malloc() implementation
 *
 * Copyright 2025 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <linux/sizes.h>
#include <malloc.h>
#include <mapmem.h>
#include <stdlib.h>
#include <asm/global_data.h>
#include <env_internal.h>
#include <test/common.h>
#include <test/test.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * get_alloced_size() - Get currently allocated memory size
 *
 * Return: Number of bytes currently allocated (not freed)
 */
static int get_alloced_size(void)
{
	struct mallinfo info = mallinfo();

	return info.uordblks;
}

/* Test basic malloc() and free() */
static int common_test_malloc_basic(struct unit_test_state *uts)
{
	int before;
	void *ptr;

	before = get_alloced_size();

	ptr = malloc(100);
	ut_assertnonnull(ptr);

	ut_assert(get_alloced_size() >= before + 100);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_basic, 0);

/* Test malloc() with zero size and free(NULL) */
static int common_test_malloc_zero(struct unit_test_state *uts)
{
	int before;
	void *ptr;

	before = get_alloced_size();

	ptr = malloc(0);
	ut_assertnonnull(ptr);
	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_zero, 0);

/* Test calloc() zeros memory */
static int common_test_calloc(struct unit_test_state *uts)
{
	int before, i;
	char *ptr;

	before = get_alloced_size();

	ptr = calloc(100, 1);
	ut_assertnonnull(ptr);

	for (i = 0; i < 100; i++)
		ut_asserteq(0, ptr[i]);

	ut_assert(get_alloced_size() >= before + 100);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_calloc, 0);

/* Test realloc() to larger size */
static int common_test_realloc_larger(struct unit_test_state *uts)
{
	char *ptr, *ptr2;
	int before, i;

	before = get_alloced_size();

	ptr = malloc(50);
	ut_assertnonnull(ptr);

	for (i = 0; i < 50; i++)
		ptr[i] = i;

	ptr2 = realloc(ptr, 100);
	ut_assertnonnull(ptr2);

	/*
	 * Check original data preserved
	 */
	for (i = 0; i < 50; i++)
		ut_asserteq(i, ptr2[i]);

	free(ptr2);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_realloc_larger, 0);

/* Test realloc() to smaller size */
static int common_test_realloc_smaller(struct unit_test_state *uts)
{
	char *ptr, *ptr2;
	int before, i;

	before = get_alloced_size();

	ptr = malloc(100);
	ut_assertnonnull(ptr);

	for (i = 0; i < 100; i++)
		ptr[i] = i;

	ptr2 = realloc(ptr, 50);
	ut_assertnonnull(ptr2);

	/*
	 * Check data preserved
	 */
	for (i = 0; i < 50; i++)
		ut_asserteq(i, ptr2[i]);

	free(ptr2);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_realloc_smaller, 0);

/* Test realloc() with NULL pointer (should act like malloc) */
static int common_test_realloc_null(struct unit_test_state *uts)
{
	int before;
	void *ptr;

	before = get_alloced_size();

	ptr = realloc(NULL, 100);
	ut_assertnonnull(ptr);
	ut_assert(get_alloced_size() >= before + 100);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_realloc_null, 0);

/*
 * Test realloc() with zero size
 *
 * Standard dlmalloc behavior (without REALLOC_ZERO_BYTES_FREES):
 * realloc(ptr, 0) returns a minimum-sized allocation.
 */
static int common_test_realloc_zero(struct unit_test_state *uts)
{
	void *ptr, *ptr2;
	int before;

	before = get_alloced_size();

	ptr = malloc(100);
	ut_assertnonnull(ptr);
	ut_assert(get_alloced_size() >= before + 100);

	ptr2 = realloc(ptr, 0);

	/*
	 * dlmalloc returns a minimum-sized allocation for realloc(ptr, 0)
	 * since REALLOC_ZERO_BYTES_FREES is not enabled.
	 * It may realloc in-place or return a different pointer.
	 */
	ut_assertnonnull(ptr2);

	free(ptr2);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_realloc_zero, 0);

/* Test memalign() with various alignments */
static int common_test_memalign(struct unit_test_state *uts)
{
	int before;
	void *ptr;

	before = get_alloced_size();

	/*
	 * Test power-of-2 alignments
	 */
	ptr = memalign(16, 100);
	ut_assertnonnull(ptr);
	ut_asserteq(0, (ulong)ptr & 0xf);
	free(ptr);

	ptr = memalign(256, 100);
	ut_assertnonnull(ptr);
	ut_asserteq(0, (ulong)ptr & 0xff);
	free(ptr);

	ptr = memalign(4096, 100);
	ut_assertnonnull(ptr);
	ut_asserteq(0, (ulong)ptr & 0xfff);
	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_memalign, 0);

/* Test multiple allocations */
static int common_test_malloc_multiple(struct unit_test_state *uts)
{
	int expected = 0, before, i;
	void *ptrs[10];

	before = get_alloced_size();

	/* Allocate multiple blocks */
	for (i = 0; i < 10; i++) {
		ptrs[i] = malloc((i + 1) * 100);
		ut_assertnonnull(ptrs[i]);
		expected += (i + 1) * 100;
	}

	/* Should have allocated at least the requested amount */
	ut_assert(get_alloced_size() >= before + expected);

	/* Free in reverse order */
	for (i = 9; i >= 0; i--)
		free(ptrs[i]);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_multiple, 0);

/* Test malloc() failure when testing enabled */
static int common_test_malloc_failure(struct unit_test_state *uts)
{
	void *ptr1, *ptr2, *ptr3;
	int before;

	before = get_alloced_size();

	/* Enable failure after 2 allocations */
	malloc_enable_testing(2);

	ptr1 = malloc(100);
	ut_assertnonnull(ptr1);

	ptr2 = malloc(100);
	ut_assertnonnull(ptr2);

	/* This should fail */
	ptr3 = malloc(100);
	ut_assertnull(ptr3);

	malloc_disable_testing();

	/* Should work again */
	ptr3 = malloc(100);
	ut_assertnonnull(ptr3);

	free(ptr1);
	free(ptr2);
	free(ptr3);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_failure, 0);

/* Test realloc() failure when testing enabled */
static int common_test_realloc_failure(struct unit_test_state *uts)
{
	void *ptr1, *ptr2;
	int before;

	before = get_alloced_size();

	ptr1 = malloc(50);
	ut_assertnonnull(ptr1);

	/* Enable failure after 0 allocations */
	malloc_enable_testing(0);

	/* This should fail and return NULL, leaving ptr1 intact */
	ptr2 = realloc(ptr1, 100);
	ut_assertnull(ptr2);

	malloc_disable_testing();

	/* ptr1 should still be valid, try to realloc it */
	ptr2 = realloc(ptr1, 100);
	ut_assertnonnull(ptr2);

	free(ptr2);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_realloc_failure, 0);

/* Test large allocation */
static int common_test_malloc_large(struct unit_test_state *uts)
{
	int size = SZ_1M, before;
	void *ptr;

	/* Skip if heap is too small for a 1MB allocation */
	if (TOTAL_MALLOC_LEN < SZ_2M)
		return -EAGAIN;

	before = get_alloced_size();

	ptr = malloc(size);
	ut_assertnonnull(ptr);
	memset(ptr, 0x5a, size);

	ut_assert(get_alloced_size() >= before + size);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_large, 0);

/* Test many small allocations (tests binning) */
static int common_test_malloc_small_bins(struct unit_test_state *uts)
{
	int after_free, before, i;
	void *ptrs[100];

	before = get_alloced_size();

	/* Allocate many small blocks of various sizes */
	for (i = 0; i < 100; i++) {
		ptrs[i] = malloc((i % 32) + 8);
		ut_assertnonnull(ptrs[i]);
	}

	/* Free every other one to create fragmentation */
	for (i = 0; i < 100; i += 2)
		free(ptrs[i]);

	after_free = get_alloced_size();

	/* Allocate more to test reuse */
	for (i = 0; i < 100; i += 2) {
		ptrs[i] = malloc((i % 32) + 8);
		ut_assertnonnull(ptrs[i]);
	}

	/* Should be back to roughly the same size (may vary due to overhead) */
	ut_assert(get_alloced_size() >= after_free);

	/* Free all */
	for (i = 0; i < 100; i++)
		free(ptrs[i]);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_small_bins, 0);

/* Test alternating allocation sizes */
static int common_test_malloc_alternating(struct unit_test_state *uts)
{
	void *small1, *large1, *small2, *large2;
	int before;

	before = get_alloced_size();

	small1 = malloc(32);
	ut_assertnonnull(small1);

	large1 = malloc(8192);
	ut_assertnonnull(large1);

	small2 = malloc(64);
	ut_assertnonnull(small2);

	large2 = malloc(16384);
	ut_assertnonnull(large2);

	ut_assert(get_alloced_size() >= before + 32 + 8192 + 64 + 16384);

	free(small1);
	free(large1);
	free(small2);
	free(large2);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_alternating, 0);

/* Test malloc() with boundary sizes */
static int common_test_malloc_boundaries(struct unit_test_state *uts)
{
	int before;
	void *ptr;

	before = get_alloced_size();

	/* Test allocation right at small/large boundary (typically 256 bytes) */
	ptr = malloc(256);
	ut_assertnonnull(ptr);
	free(ptr);

	ptr = malloc(255);
	ut_assertnonnull(ptr);
	free(ptr);

	ptr = malloc(257);
	ut_assertnonnull(ptr);
	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_boundaries, 0);

/* Test malloc_usable_size() */
static int common_test_malloc_usable_size(struct unit_test_state *uts)
{
	int before, size;
	void *ptr;

	before = get_alloced_size();

	ptr = malloc(100);
	ut_assertnonnull(ptr);

	size = malloc_usable_size(ptr);
	/* Usable size should be at least the requested size */
	ut_assert(size >= 100);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_usable_size, 0);

/* Test mallinfo() returns reasonable values */
static int common_test_mallinfo(struct unit_test_state *uts)
{
	void *ptr1, *ptr2, *ptr3;
	struct mallinfo info;
	int arena_before;
	int used_after1;
	int used_after2;
	int before;

	before = get_alloced_size();

	info = mallinfo();
	arena_before = info.arena;

	ptr1 = malloc(1024);
	ut_assertnonnull(ptr1);

	info = mallinfo();
	/* Arena size should not change (it's the total heap size) */
	ut_asserteq(arena_before, info.arena);
	/* Used memory should increase */
	ut_assert(info.uordblks >= before + 1024);
	used_after1 = info.uordblks;

	ptr2 = malloc(2048);
	ut_assertnonnull(ptr2);

	info = mallinfo();
	ut_asserteq(arena_before, info.arena);
	ut_assert(info.uordblks >= used_after1 + 2048);
	used_after2 = info.uordblks;

	ptr3 = malloc(512);
	ut_assertnonnull(ptr3);

	info = mallinfo();
	ut_asserteq(arena_before, info.arena);
	ut_assert(info.uordblks >= used_after2 + 512);

	free(ptr1);
	free(ptr2);
	free(ptr3);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_mallinfo, 0);

/* Test allocating a very large size */
static int common_test_malloc_very_large(struct unit_test_state *uts)
{
	size_t size, before, margin;
	void *ptr;

	before = get_alloced_size();

	/*
	 * Target the largest free chunk rather than the whole pool, so earlier
	 * test leaks that fragment the heap do not turn this into a flakily
	 * failing test. When mcheck is enabled, allow for the per-allocation
	 * header and canaries; otherwise a smaller margin for dlmalloc's own
	 * chunk alignment is enough.
	 */
	if (CONFIG_IS_ENABLED(MCHECK_HEAP_PROTECTION))
		margin = SZ_256K;
	else
		margin = SZ_64K;
	size = malloc_largest_free() - margin;

	ptr = malloc(size);
	ut_assertnonnull(ptr);
	ut_assert(get_alloced_size() >= before + size);

	free(ptr);

	ut_asserteq(before, get_alloced_size());

	return 0;
}
COMMON_TEST(common_test_malloc_very_large, 0);

/* Test allocating the full malloc pool size */
static int common_test_malloc_full_pool(struct unit_test_state *uts)
{
	/* Try to allocate the full pool size - should fail due to overhead */
	ut_assertnull(malloc(TOTAL_MALLOC_LEN));

	return 0;
}
COMMON_TEST(common_test_malloc_full_pool, 0);

/* Test filling the entire malloc pool with allocations */
static int common_test_malloc_fill_pool(struct unit_test_state *uts)
{
	int alloc_size, before, count, i, total;
	const int ptr_table_size = 0x100000;
	size_t largest;
	void **ptrs;
	void *ptr;

	/*
	 * this is only really safe on sandbox since it uses up all memory and
	 * assumed that at least half of the malloc() pool is unallocated
	 */
	if (!IS_ENABLED(CONFIG_SANDBOX))
		return -EAGAIN;

	before = get_alloced_size();

	/*
	 * Record the largest contiguous free region up front. Earlier tests
	 * may have left scattered leaks that fragment the heap, so cap the
	 * assertion below against what is actually available rather than the
	 * whole pool.
	 */
	largest = malloc_largest_free();

	/* Use memory outside malloc pool to store pointers */
	ptrs = map_sysmem(0x1000, ptr_table_size);

	/* Allocate until we run out of memory, using random sizes */
	count = 0;
	total = 0;
	while (1) {
		/* Random size up to 1 MB */
		alloc_size = rand() % (SZ_1M);
		ptr = malloc(alloc_size);
		if (!ptr)
			break;
		ptrs[count++] = ptr;
		total += alloc_size;
		/* Safety check to avoid infinite loop */
		if (count >= ptr_table_size / sizeof(void *))
			break;
	}
	printf("count %d total %d ptr_table_size %d\n", count, total,
	       ptr_table_size);

	/*
	 * Should have allocated most of the available pool - if we can't
	 * allocate 1 MB, then at most 1 MB is available, so we must have
	 * allocated at least (available - 1 MB). Save the peak before
	 * freeing so an assertion failure does not leak the entire pool.
	 */
	ut_assert(count > 0);
	ut_assert(count < ptr_table_size / sizeof(void *));
	alloc_size = get_alloced_size();

	/*
	 * Free all allocations before checking the peak, so that a failure does
	 * not leak the entire pool and break later tests
	 */
	for (i = 0; i < count; i++)
		free(ptrs[i]);

	ut_assert(alloc_size - before >= largest - SZ_1M);

	/* Should be back to starting state */
	ut_asserteq(before, get_alloced_size());

	/* Verify we can allocate a large block again */
	ptr = malloc(largest / 2);
	ut_assertnonnull(ptr);
	free(ptr);

	unmap_sysmem(ptrs);

	return 0;
}
COMMON_TEST(common_test_malloc_fill_pool, 0);

#if CONFIG_IS_ENABLED(MCHECK_LOG)
/* Test malloc_log_info() and malloc_log_entry() */
static int common_test_malloc_log_info(struct unit_test_state *uts)
{
	struct mlog_entry *entry;
	struct mlog_info info;
	void *ptr, *ptr2;

	malloc_log_start();

	/* Do an allocation, realloc, and free */
	ptr = malloc(0x100);
	ut_assertnonnull(ptr);

	ptr2 = realloc(ptr, 0x200);
	ut_assertnonnull(ptr2);

	free(ptr2);

	malloc_log_stop();

	ut_assertok(malloc_log_info(&info));
	ut_asserteq(3, info.entry_count);
	ut_assert(info.max_entries > 0);
	ut_asserteq(info.entry_count, info.total_count);  /* no wrapping */

	/* Check the alloc entry */
	ut_assertok(malloc_log_entry(0, &entry));
	ut_asserteq(MLOG_ALLOC, entry->type);
	ut_asserteq_ptr(ptr, entry->ptr);
	ut_asserteq(0x100, entry->size);

	/* Check the realloc entry */
	ut_assertok(malloc_log_entry(1, &entry));
	ut_asserteq(MLOG_REALLOC, entry->type);
	ut_asserteq_ptr(ptr2, entry->ptr);
	ut_asserteq(0x200, entry->size);
	ut_asserteq(0x100, entry->old_size);

	/* Check the free entry */
	ut_assertok(malloc_log_entry(2, &entry));
	ut_asserteq(MLOG_FREE, entry->type);
	ut_asserteq_ptr(ptr2, entry->ptr);

	/* Out of range should fail */
	ut_asserteq(-ERANGE, malloc_log_entry(3, &entry));

	return 0;
}
COMMON_TEST(common_test_malloc_log_info, 0);

/* Test malloc_log_dump() */
static int common_test_malloc_log_dump(struct unit_test_state *uts)
{
	struct mlog_info info;
	void *ptr, *ptr2;

	malloc_log_start();

	/* Do an allocation, realloc, and free */
	ptr = malloc(0x100);
	ut_assertnonnull(ptr);

	ptr2 = realloc(ptr, 0x200);
	ut_assertnonnull(ptr2);

	free(ptr2);

	malloc_log_stop();

	ut_assertok(malloc_log_info(&info));

	console_record_reset_enable();
	malloc_log_dump();

	ut_assert_nextline("Malloc log: 3 entries (max %u, total 3)",
			   info.max_entries);
	ut_assert_nextline("%4s  %-8s  %10s  %8s  %s", "Seq", "Type", "Address",
			   "Size", "Caller");
	ut_assert_nextline("----  --------  ----------  --------  ------");
	ut_assert_nextlinen("   0  alloc     %10lx       100",
			    (ulong)map_to_sysmem(ptr));
	ut_assert_nextlinen("   1  realloc   %10lx       200 (was 100)",
			    (ulong)map_to_sysmem(ptr2));
	ut_assert_nextlinen("   2  free      %10lx         0",
			    (ulong)map_to_sysmem(ptr2));
	ut_assert_console_end();

	return 0;
}
COMMON_TEST(common_test_malloc_log_dump, 0);
#endif /* MCHECK_LOG */
