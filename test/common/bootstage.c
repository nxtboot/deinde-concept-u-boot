// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for bootstage API
 *
 * Copyright 2025 Canonical Ltd
 */

#include <bootstage.h>
#include <malloc.h>
#include <asm/global_data.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <test/common.h>
#include <test/test.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test bootstage_mark_name() */
static int test_bootstage_mark(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	ulong time;
	int count;

	/* Get the current count so we know where our record will be */
	count = bootstage_get_rec_count();

	/* Mark a stage and verify we get a valid timestamp */
	time = bootstage_mark_name(BOOTSTAGE_ID_USER + 50, "test_stage_mark");
	ut_assert(time > 0);

	/* Verify the count increased by 1 */
	ut_asserteq(count + 1, bootstage_get_rec_count());

	/* Check that the record was added correctly */
	rec = bootstage_get_rec(count);
	ut_assertnonnull(rec);
	ut_asserteq(BOOTSTAGE_ID_USER + 50, rec->id);
	ut_asserteq_str("test_stage_mark", rec->name);
	ut_asserteq(time, rec->time_us);
	ut_asserteq(0, rec->flags);
	ut_asserteq(time, bootstage_get_time(BOOTSTAGE_ID_USER + 50));

	/* A mark is not an accumulator, so it never counts a run */
	ut_asserteq(0, rec->run_cnt);

	/* Restore the original count */
	bootstage_set_rec_count(count);

	return 0;
}
COMMON_TEST(test_bootstage_mark, 0);

/* Test bootstage_error_name() */
static int test_bootstage_error(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	ulong time;
	int count;

	count = bootstage_get_rec_count();

	/* Mark an error stage and verify we get a valid timestamp */
	time = bootstage_error_name(BOOTSTAGE_ID_USER + 51, "test_error");
	ut_assert(time > 0);

	/* Check the error record */
	rec = bootstage_get_rec(count);
	ut_assertnonnull(rec);
	ut_asserteq(BOOTSTAGE_ID_USER + 51, rec->id);
	ut_asserteq_str("test_error", rec->name);
	ut_asserteq(time, rec->time_us);
	ut_asserteq(BOOTSTAGEF_ERROR, rec->flags);

	/* Restore the original count */
	bootstage_set_rec_count(count);

	return 0;
}
COMMON_TEST(test_bootstage_error, 0);

/* Test bootstage_start() and bootstage_accum() */
static int test_bootstage_accum(struct unit_test_state *uts)
{
	enum bootstage_id id = BOOTSTAGE_ID_USER + 53;
	uint start_time, elapsed1, elapsed2;
	const struct bootstage_record *rec;
	int index, count;

	count = bootstage_get_rec_count();

	/* Start an accumulator */
	start_time = bootstage_start(id, "test_accum");
	ut_assert(start_time > 0);

	/* Check the accumulator record was created */
	index = count;
	rec = bootstage_get_rec(index);
	ut_assertnonnull(rec);
	ut_asserteq(id, rec->id);
	ut_asserteq_str("test_accum", rec->name);
	ut_asserteq(start_time, rec->start_us);

	/* Accumulate the time */
	udelay(1);
	elapsed1 = bootstage_accum(id);
	ut_assert(elapsed1 >= 0);

	/* Check the accumulated time was recorded */
	ut_asserteq(elapsed1, rec->time_us);
	ut_asserteq(1, rec->run_cnt);

	/* Start and accumulate again  */
	bootstage_start(id, "test_accum");
	udelay(1);
	elapsed2 = bootstage_accum(id);
	ut_assert(elapsed2 >= 0);

	/* Check the total time accumulated */
	rec = bootstage_get_rec(index);
	ut_asserteq(rec->time_us, elapsed1 + elapsed2);
	ut_asserteq(rec->time_us, bootstage_get_time(id));

	/* Both runs must be counted, since that is what gives the average */
	ut_asserteq(2, rec->run_cnt);

	/* Restore the original count */
	bootstage_set_rec_count(count);

	return 0;
}
COMMON_TEST(test_bootstage_accum, 0);

/* Test that an accumulator can be used before bootstage_init() */
static int test_bootstage_early(struct unit_test_state *uts)
{
	enum bootstage_id id = BOOTSTAGE_ID_USER + 54;
	struct bootstage_data *data = gd->bootstage;
	ulong start;
	uint accum;
	int count;

	count = bootstage_get_rec_count();

	/*
	 * Pretend bootstage is not set up yet, as it is not when the earliest
	 * code runs. Put it back before checking anything, so that a failure
	 * here does not leave bootstage broken for later tests
	 */
	gd->bootstage = NULL;
	start = bootstage_start(id, "test_early");
	accum = bootstage_accum(id);
	gd->bootstage = data;

	/* The timestamp is still useful, even with nowhere to record it */
	ut_assert(start > 0);
	ut_asserteq(0, accum);

	/* Nothing may have been recorded */
	ut_asserteq(count, bootstage_get_rec_count());

	return 0;
}
COMMON_TEST(test_bootstage_early, 0);

/* Test bootstage_mark_code() */
static int test_bootstage_mark_code(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	ulong time;
	int count;

	count = bootstage_get_rec_count();

	/* Mark with file, function, and line number */
	time = bootstage_mark_code("file.c", __func__, 123);
	ut_assert(time > 0);

	/* Check the record */
	rec = bootstage_get_rec(count);
	ut_assertnonnull(rec);
	ut_asserteq(time, rec->time_us);
	ut_asserteq_str("file.c,123: test_bootstage_mark_code", rec->name);

	/* Restore the original count */
	bootstage_set_rec_count(count);

	return 0;
}
COMMON_TEST(test_bootstage_mark_code, 0);

/* Test bootstage_get_rec_count() */
static int test_bootstage_get_rec_count(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	int orig, count;

	/* Get initial count */
	orig = bootstage_get_rec_count();
	ut_assert(orig > 0);

	/* Add a new record */
	bootstage_mark_name(BOOTSTAGE_ID_USER + 52, "test_count");

	/* Verify count increased */
	count = bootstage_get_rec_count();
	ut_asserteq(orig + 1, count);

	/* Verify the record was added at the correct index */
	rec = bootstage_get_rec(orig);
	ut_assertnonnull(rec);
	ut_asserteq(BOOTSTAGE_ID_USER + 52, rec->id);
	ut_asserteq_str("test_count", rec->name);

	/* Restore the original count */
	bootstage_set_rec_count(orig);

	return 0;
}
COMMON_TEST(test_bootstage_get_rec_count, 0);

/* Test bootstage_get_rec() */
static int test_bootstage_get_rec(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	int count;

	/* Get total count */
	count = bootstage_get_rec_count();
	ut_assert(count > 0);

	/* Get first record (should be "reset") */
	rec = bootstage_get_rec(0);
	ut_assertnonnull(rec);
	ut_asserteq_str("reset", rec->name);

	/* Test out of bounds access */
	ut_assertnull(bootstage_get_rec(count));
	ut_assertnull(bootstage_get_rec(count + 100));
	ut_assertnull(bootstage_get_rec(-1));

	return 0;
}
COMMON_TEST(test_bootstage_get_rec, 0);

/* Test that relocation stays inside the space which was reserved for it */
static int test_bootstage_relocate(struct unit_test_state *uts)
{
	struct bootstage_data *orig = gd->bootstage;
	const struct bootstage_record *rec;
	int count, size, small;
	char *buf, *small_buf;

	count = bootstage_get_rec_count();
	ut_assert(count > 0);

	/*
	 * An accumulator creates its record without a name, so this checks
	 * that a nameless record is handled the same way everywhere
	 */
	bootstage_accum(BOOTSTAGE_ID_USER + 60);

	size = bootstage_get_size(true);
	buf = malloc(size + 4);
	ut_assertnonnull(buf);
	strcpy(buf + size, "grd");

	ut_assertok(bootstage_relocate(buf, size));

	/* Nothing may be written past the space which was reserved */
	ut_asserteq_str("grd", buf + size);

	/* The names must have come across */
	rec = bootstage_get_rec(0);
	ut_assertnonnull(rec);
	ut_asserteq_str("reset", rec->name);

	/*
	 * Records can be added after the space is reserved, so relocation must
	 * cope with there being too little room for the names. Use a buffer of
	 * exactly the smaller size, so that writing past it is detected
	 */
	small = size - 20;
	ut_assert(small > 0);
	small_buf = malloc(small + 4);
	ut_assertnonnull(small_buf);
	strcpy(small_buf + small, "grd");

	gd->bootstage = orig;
	ut_assertok(bootstage_relocate(small_buf, small));
	ut_asserteq_str("grd", small_buf + small);

	gd->bootstage = orig;
	bootstage_set_rec_count(count);
	free(small_buf);
	free(buf);

	return 0;
}
COMMON_TEST(test_bootstage_relocate, 0);

#if IS_ENABLED(CONFIG_BOOTSTAGE_STASH)
/* Test that records survive a stash and unstash */
static int test_bootstage_stash(struct unit_test_state *uts)
{
	const struct bootstage_record *rec;
	int count;
	void *buf;

	count = bootstage_get_rec_count();
	ut_assert(count > 0);
	ut_assert(count * 2 <= CONFIG_BOOTSTAGE_RECORD_COUNT);

	/*
	 * Use a buffer of our own. The configured stash address may well hold
	 * something else, since nothing reads it until a later phase starts
	 */
	buf = malloc(CONFIG_BOOTSTAGE_STASH_SIZE);
	ut_assertnonnull(buf);

	/*
	 * Stash the records and read them back. Unstashing appends, so every
	 * record which survives the round trip appears a second time
	 */
	ut_assertok(bootstage_stash(buf, CONFIG_BOOTSTAGE_STASH_SIZE));
	ut_assertok(bootstage_unstash(buf, CONFIG_BOOTSTAGE_STASH_SIZE));
	ut_asserteq(count * 2, bootstage_get_rec_count());

	/* The copy must carry the names too, not just the timestamps */
	rec = bootstage_get_rec(count);
	ut_assertnonnull(rec);
	ut_asserteq_str("reset", rec->name);

	bootstage_set_rec_count(count);

	/*
	 * A stash written by a different version must be refused, since the
	 * record layout may have changed. The version is the first word
	 */
	ut_assertok(bootstage_stash(buf, CONFIG_BOOTSTAGE_STASH_SIZE));
	*(u32 *)buf = 0xdeadbeef;
	ut_asserteq(-EINVAL,
		    bootstage_unstash(buf, CONFIG_BOOTSTAGE_STASH_SIZE));

	/* Nothing may have been added by the rejected stash */
	ut_asserteq(count, bootstage_get_rec_count());
	free(buf);

	/*
	 * Check the default stash as well, but only when it is in the
	 * bloblist, which has memory of its own. A fixed address cannot be
	 * written here, since it may be in use until a later phase reads it
	 */
	if (IS_ENABLED(CONFIG_BOOTSTAGE_STASH_BLOBLIST)) {
		ut_assertok(bootstage_stash_default());
		ut_assertok(bootstage_unstash_default());
		ut_asserteq(count * 2, bootstage_get_rec_count());
		bootstage_set_rec_count(count);
	}

	return 0;
}
COMMON_TEST(test_bootstage_stash, 0);
#endif /* BOOTSTAGE_STASH */
