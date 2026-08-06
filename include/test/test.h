/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2013 Google, Inc.
 */

#ifndef __TEST_TEST_H
#define __TEST_TEST_H

#include <abuf.h>
#include <malloc.h>
#include <linux/bitops.h>
#include <linux/list.h>

#define UT_MAX_ARGS	8
#define UT_PRIV_SIZE	256

/**
 * struct ut_stats - Statistics about tests run
 *
 * @fail_count: Number of tests that failed
 * @skip_count: Number of tests that were skipped
 * @test_count: Number of tests run. If a test is run muiltiple times, only one
 *	is counted
 * @start: Timer value when test started
 * @duration_ms: Suite duration in milliseconds
 */
struct ut_stats {
	int fail_count;
	int skip_count;
	int test_count;
	ulong start;
	ulong duration_ms;
};

/**
 * enum ut_arg_type - Type of a unit test argument
 *
 * @UT_ARG_INT: Integer argument (hex with 0x prefix, or decimal) -> vint
 * @UT_ARG_BOOL: Boolean argument (0 or 1) -> vbool
 * @UT_ARG_STR: String argument -> vstr
 */
enum ut_arg_type {
	UT_ARG_INT,
	UT_ARG_BOOL,
	UT_ARG_STR,
};

/**
 * struct ut_arg - Parsed unit test argument value
 *
 * Holds the parsed value of an argument after command-line processing.
 *
 * @name: Name of the argument (points to ut_arg_def.name)
 * @type: Type of the argument
 * @provided: true if value was provided on command line
 * @vint: Integer value (when type is UT_ARG_INT)
 * @vbool: Boolean value (when type is UT_ARG_BOOL)
 * @vstr: String value (when type is UT_ARG_STR, points into argv)
 */
struct ut_arg {
	const char *name;
	enum ut_arg_type type;
	bool provided;
	union {
		long vint;
		bool vbool;
		const char *vstr;
	};
};

/*
 * struct unit_test_state - Entire state of test system
 *
 * @cur: Statistics for the current run
 * @total: Statistics for all test runs
 * @run_count: Number of times ut_run_list() has been called
 * @worst: Sute which had the first per-text run time
 * @worst_ms: Time taken by that test
 * @start: Store the starting mallinfo when doing leak test
 * @of_live: true to use livetree if available, false to use flattree
 * @of_root: Record of the livetree root node (used for setting up tests)
 * @saved_dm_root: Global dm_root swapped out while a UTF_DM test runs
 * @saved_uclass_root: Global uclass-root list head swapped out while a
 *	UTF_DM test runs, so the pre-test state can be restored afterwards
 * @saved_uclass_root_ptr: Saved value of gd->uclass_root (the pointer to
 *	the uclass-root list head). Restored alongside the list head above
 *	in case a test zeros the pointer (e.g. dm_test_uclass_before_ready)
 * @root: Root device
 * @testdev: Test device
 * @force_fail_alloc: Force all memory allocs to fail
 * @skip_post_probe: Skip uclass post-probe processing
 * @fdt_chksum: crc8 of the device tree contents
 * @fdt_copy: Copy of the device tree
 * @fdt_size: Size of the device-tree copy
 * @other_fdt: Buffer for the other FDT (UTF_OTHER_FDT)
 * @other_fdt_size: Size of the other FDT (UTF_OTHER_FDT)
 * @of_other: Live tree for the other FDT
 * @runs_per_test: Number of times to run each test (typically 1)
 * @force_run: true to run tests marked with the UTF_MANUAL flag
 * @workers: Number of parallel workers, 0 if not sharding tests
 * @worker_id: ID of this worker (0 to workers-1)
 * @old_bootstage_count: bootstage record count saved before each test
 * @old_bloblist: stores the old gd->bloblist pointer
 * @soft_fail: continue execution of the test even after it fails
 * @expect_str: Temporary string used to hold expected string value
 * @actual_str: Temporary string used to hold actual string value
 * @args: Parsed argument values for current test
 * @arg_count: Number of parsed arguments
 * @arg_error: Set if ut_str/int/bool() detects a type mismatch
 * @keep_record: Preserve console recording when ut_fail() is called
 * @emit_result: Emit result line after each test completes
 * @leak_check: Check for memory leaks around each test
 * @video_ctx: Vidconsole context for test message display (allocated on use)
 * @video_save: Saved framebuffer region for video tests
 * @priv: Private data for tests to use as needed
 */
struct unit_test_state {
	struct ut_stats cur;
	struct ut_stats total;
	int run_count;
	const struct suite *worst;
	int worst_ms;
	struct mallinfo start;
	struct device_node *of_root;
	struct udevice *saved_dm_root;
	struct list_head saved_uclass_root;
	struct list_head *saved_uclass_root_ptr;
	bool of_live;
	struct udevice *root;
	struct udevice *testdev;
	int force_fail_alloc;
	int skip_post_probe;
	uint fdt_chksum;
	void *fdt_copy;
	uint fdt_size;
	void *other_fdt;
	int other_fdt_size;
	struct device_node *of_other;
	int runs_per_test;
	bool force_run;
	int workers;
	int worker_id;
	uint old_bootstage_count;
	void *old_bloblist;
	bool soft_fail;
	char expect_str[1024];
	char actual_str[1024];
	struct ut_arg args[UT_MAX_ARGS];
	int arg_count;
	bool arg_error;
	bool keep_record;
	bool emit_result;
	bool leak_check;
	void *video_ctx;
	struct abuf video_save;
	char priv[UT_PRIV_SIZE];
};

/* Test flags for each test */
enum ut_flags {
	UTF_SCAN_PDATA	= BIT(0),	/* test needs platform data */
	UTF_PROBE_TEST	= BIT(1),	/* probe test uclass */
	UTF_SCAN_FDT	= BIT(2),	/* scan device tree */
	UTF_FLAT_TREE	= BIT(3),	/* test needs flat DT */
	UTF_LIVE_TREE	= BIT(4),	/* needs live device tree */
	UTF_CONSOLE	= BIT(5),	/* needs console recording */
	UTF_NO_SILENT	= BIT(6),	/* console cannot be silent */
	/* do extra driver model init and uninit */
	UTF_DM		= BIT(7),
	UTF_OTHER_FDT	= BIT(8),	/* read in other device tree */
	/*
	 * Only run if explicitly requested with 'ut -f <suite> <test>'. The
	 * test name must end in "_norun" so that pytest detects this also,
	 * since it cannot access the flags.
	 */
	UTF_MANUAL	= BIT(9),
	UTF_ETH_BOOTDEV	= BIT(10),	/* enable Ethernet bootdevs */
	UTF_SF_BOOTDEV	= BIT(11),	/* enable SPI flash bootdevs */
	UFT_BLOBLIST	= BIT(12),	/* test changes gd->bloblist */
	UTF_INIT	= BIT(13),	/* test inits a suite */
	UTF_UNINIT	= BIT(14),	/* test uninits a suite */
};

/**
 * enum ut_arg_flags - Flags for unit test arguments
 *
 * @UT_ARGF_OPTIONAL: Argument is optional; use default value if not provided
 */
enum ut_arg_flags {
	UT_ARGF_OPTIONAL	= BIT(0),
};

/**
 * struct ut_arg_def - Definition of a unit test argument
 *
 * Declares an expected argument for a test, including its name, type,
 * whether it is optional, and its default value.
 *
 * @name: Name of the argument (used in key=value matching)
 * @type: Type of the argument (int, bool, or string)
 * @flags: Argument flags (e.g., UT_ARGF_OPTIONAL)
 * @def: Default value (used when argument is optional and not provided)
 */
struct ut_arg_def {
	const char *name;
	enum ut_arg_type type;
	int flags;
	union {
		long vint;
		bool vbool;
		const char *vstr;
	} def;
};

/**
 * struct unit_test - Information about a unit test
 *
 * @name: Name of test
 * @func: Function to call to perform test
 * @flags: Flags indicated pre-conditions for test
 * @arg_defs: Argument definitions (NULL-terminated array), or NULL
 */
struct unit_test {
	const char *file;
	const char *name;
	int (*func)(struct unit_test_state *state);
	int flags;
	const struct ut_arg_def *arg_defs;
};

/**
 * UNIT_TEST() - create linker generated list entry for unit a unit test
 *
 * The macro UNIT_TEST() is used to create a linker generated list entry. These
 * list entries are enumerate tests that can be execute using the ut command.
 * The list entries are used both by the implementation of the ut command as
 * well as in a related Python test.
 *
 * For Python testing the subtests are collected in Python function
 * generate_ut_subtest() by applying a regular expression to the lines of file
 * u-boot.sym. The list entries have to follow strict naming conventions to be
 * matched by the expression.
 *
 * Use UNIT_TEST(foo_test_bar, _flags, foo_test) for a test bar in test suite
 * foo that can be executed via command 'ut foo bar' and is implemented in
 * function foo_test_bar().
 *
 * @_name:	concatenation of name of the test suite, "_test_", and the name
 *		of the test
 * @_flags:	an integer field that can be evaluated by the test suite
 *		implementation (see enum ut_flags)
 * @_suite:	name of the test suite concatenated with "_test"
 */
#define UNIT_TEST(_name, _flags, _suite)				\
	ll_entry_declare(struct unit_test, _name, ut_ ## _suite) = {	\
		.file = __FILE__,					\
		.name = #_name,						\
		.flags = _flags,					\
		.func = _name,						\
	}

/* init function for unit-test suite (the 'A' makes it first) */
#define UNIT_TEST_INIT(_name, _flags, _suite)				\
	ll_entry_declare(struct unit_test, A ## _name, ut_ ## _suite) = {	\
		.file = __FILE__,					\
		.name = #_name,						\
		.flags = (_flags) | UTF_INIT,				\
		.func = _name,						\
	}

/* uninit function for unit-test suite (the 'aaa' makes it last) */
#define UNIT_TEST_UNINIT(_name, _flags, _suite)				\
	ll_entry_declare(struct unit_test, zzz ## _name, ut_ ## _suite) = { \
		.file = __FILE__,					\
		.name = #_name,						\
		.flags = (_flags) | UTF_UNINIT,				\
		.func = _name,						\
	}

/**
 * UNIT_TEST_ARGS() - create unit test entry with inline argument definitions
 *
 * Like UNIT_TEST() but allows specifying argument definitions inline.
 * The variadic arguments are struct ut_arg_def initializers. The NULL
 * terminator is added automatically by the macro.
 *
 * Example:
 *   UNIT_TEST_ARGS(my_test, UTF_CONSOLE, my_suite,
 *       { "path", UT_ARG_STR },
 *       { "count", UT_ARG_INT, UT_ARGF_OPTIONAL, { .vint = 10 } });
 *
 * @_name:	Test function name
 * @_flags:	Test flags (see enum ut_flags)
 * @_suite:	Test suite name
 * @...:	Argument definitions (struct ut_arg_def initializers)
 */
#define UNIT_TEST_ARGS(_name, _flags, _suite, ...)			\
	static const struct ut_arg_def _name##_args[] = { __VA_ARGS__, { NULL } }; \
	ll_entry_declare(struct unit_test, _name, ut_ ## _suite) = {	\
		.file = __FILE__,					\
		.name = #_name,						\
		.flags = _flags,					\
		.func = _name,						\
		.arg_defs = _name##_args,				\
	}

/* Get the start of a list of unit tests for a particular suite */
#define UNIT_TEST_SUITE_START(_suite) \
	ll_entry_start(struct unit_test, ut_ ## _suite)
#define UNIT_TEST_SUITE_COUNT(_suite) \
	ll_entry_count(struct unit_test, ut_ ## _suite)

/* Use ! and ~ so that all tests will be sorted between these two values */
#define UNIT_TEST_ALL_START()	ll_entry_start(struct unit_test, ut_!)
#define UNIT_TEST_ALL_END()	ll_entry_start(struct unit_test, ut_~)
/*
 * Compute the count via byte arithmetic. Both boundary markers are aligned
 * to CONFIG_LINKER_LIST_ALIGN, so the byte distance between them is not
 * necessarily a multiple of sizeof(struct unit_test). Plain pointer
 * subtraction is undefined behaviour in that case and, in practice, the
 * compiler's exact-division helpers return garbage.
 */
#define UNIT_TEST_ALL_COUNT()						\
	((unsigned int)((char *)UNIT_TEST_ALL_END() -			\
			(char *)UNIT_TEST_ALL_START()) /		\
	 sizeof(struct unit_test))

/* Sizes for devres tests */
enum {
	TEST_DEVRES_SIZE	= 100,
	TEST_DEVRES_COUNT	= 10,
	TEST_DEVRES_TOTAL	= TEST_DEVRES_SIZE * TEST_DEVRES_COUNT,

	/* A few different sizes */
	TEST_DEVRES_SIZE2	= 15,
	TEST_DEVRES_SIZE3	= 37,
};

/**
 * testbus_get_clear_removed() - Test function to obtain removed device
 *
 * This is used in testbus to find out which device was removed. Calling this
 * function returns a pointer to the device and then clears it back to NULL, so
 * that a future test can check it.
 */
struct udevice *testbus_get_clear_removed(void);

#ifdef CONFIG_SANDBOX
#include <asm/state.h>
#include <asm/test.h>
#endif

static inline void arch_reset_for_test(void)
{
#ifdef CONFIG_SANDBOX
	state_reset_for_test(state_get_current());
#endif
}
static inline int test_load_other_fdt(struct unit_test_state *uts)
{
	int ret = 0;
#ifdef CONFIG_SANDBOX
	ret = sandbox_load_other_fdt(&uts->other_fdt, &uts->other_fdt_size);
#endif
	return ret;
}

/**
 * Control skipping of time delays
 *
 * Some tests have unnecessay time delays (e.g. USB). Allow these to be
 * skipped to speed up testing
 *
 * @param skip_delays	true to skip delays from now on, false to honour delay
 *			requests
 */
static inline void test_set_skip_delays(bool skip_delays)
{
#ifdef CONFIG_SANDBOX
	state_set_skip_delays(skip_delays);
#endif
}

/**
 * test_set_eth_enable() - Enable / disable Ethernet
 *
 * Allows control of whether Ethernet packets are actually send/received
 *
 * @enable: true to enable Ethernet, false to disable
 */
static inline void test_set_eth_enable(bool enable)
{
#ifdef CONFIG_SANDBOX
	sandbox_set_eth_enable(enable);
#endif
}

/* Allow ethernet to be disabled for testing purposes */
static inline bool test_eth_enabled(void)
{
	bool enabled = true;

#ifdef CONFIG_SANDBOX
	enabled = sandbox_eth_enabled();
#endif
	return enabled;
}

/* Allow ethernet bootdev to be ignored for testing purposes */
static inline bool test_eth_bootdev_enabled(void)
{
	bool enabled = true;

#ifdef CONFIG_SANDBOX
	enabled = sandbox_eth_enabled();
#endif
	return enabled;
}

/* Allow SPI flash bootdev to be ignored for testing purposes */
static inline bool test_sf_bootdev_enabled(void)
{
	bool enabled = true;

#ifdef CONFIG_SANDBOX
	enabled = sandbox_sf_bootdev_enabled();
#endif
	return enabled;
}

static inline void test_sf_set_enable_bootdevs(bool enable)
{
#ifdef CONFIG_SANDBOX
	sandbox_sf_set_enable_bootdevs(enable);
#endif
}

static inline bool test_flattree_test_enabled(void)
{
#ifdef CONFIG_SANDBOX
	struct sandbox_state *state = state_get_current();

	return !state->no_flattree_tests;
#else
	return true;
#endif
}

static inline bool test_soft_fail(void)
{
#ifdef CONFIG_SANDBOX
	struct sandbox_state *state = state_get_current();

	return state->soft_fail;
#else
	return false;
#endif
}

#endif /* __TEST_TEST_H */
