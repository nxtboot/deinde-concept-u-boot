// SPDX-License-Identifier: GPL-2.0
/*
 * (C) Copyright 2015
 * Joe Hershberger, National Instruments, joe.hershberger@ni.com
 */

#include <command.h>
#include <console.h>
#include <vsprintf.h>
#include <test/test.h>
#include <test/ut.h>

/**
 * struct suite - A set of tests for a certain topic
 *
 * All tests end up in a single 'struct unit_test' linker-list array, in order
 * of the suite they are in
 *
 * @name: Name of suite
 * @start: First test in suite
 * @end: End test in suite (points to the first test in the next suite)
 * @help: Help-string to show for this suite
 */
struct suite {
	const char *name;
	struct unit_test *start;
	struct unit_test *end;
	const char *help;
};

static int do_ut_all(struct unit_test_state *uts, const char *select_name,
		     int runs_per_test, bool force_run,
		     const char *test_insert, int argc, char *const argv[]);

static int do_ut_info(bool show_suites);

/* declare linker-list symbols for the start and end of a suite */
#define SUITE_DECL(_name) \
	ll_start_decl(suite_start_ ## _name, struct unit_test, ut_ ## _name); \
	ll_end_decl(suite_end_ ## _name, struct unit_test, ut_ ## _name)

/* declare a test suite which can be run directly without a subcommand */
#define SUITE(_name, _help) { \
	#_name, \
	suite_start_ ## _name, \
	suite_end_ ## _name, \
	_help, \
	}

SUITE_DECL(addrmap);
SUITE_DECL(bdinfo);
SUITE_DECL(bloblist);
SUITE_DECL(bootctl);
SUITE_DECL(bootm);
SUITE_DECL(bootstd);
SUITE_DECL(cmd);
SUITE_DECL(common);
SUITE_DECL(dm);
SUITE_DECL(env);
SUITE_DECL(exit);
SUITE_DECL(fdt);
SUITE_DECL(fdt_overlay);
SUITE_DECL(fit_verity);
SUITE_DECL(font);
SUITE_DECL(fs);
SUITE_DECL(hush);
SUITE_DECL(image_fdt);
SUITE_DECL(lib);
SUITE_DECL(loadm);
SUITE_DECL(log);
SUITE_DECL(mbr);
SUITE_DECL(measurement);
SUITE_DECL(mem);
SUITE_DECL(optee);
SUITE_DECL(pci_mps);
SUITE_DECL(pxe);
SUITE_DECL(seama);
SUITE_DECL(setexpr);
SUITE_DECL(upl);

static struct suite suites[] = {
	SUITE(addrmap, "very basic test of addrmap command"),
	SUITE(bdinfo, "bdinfo (board info) command"),
	SUITE(bloblist, "bloblist implementation"),
	SUITE(bootctl, "bootctl (boot schema)"),
	SUITE(bootm, "bootm command"),
	SUITE(bootstd, "standard boot implementation"),
	SUITE(cmd, "various commands"),
	SUITE(common, "tests for common/ directory"),
	SUITE(dm, "driver model"),
	SUITE(env, "environment"),
	SUITE(exit, "shell exit and variables"),
	SUITE(fdt, "fdt command"),
	SUITE(fdt_overlay, "device tree overlays"),
	SUITE(fit_verity, "FIT dm-verity cmdline generation"),
	SUITE(font, "font command"),
	SUITE(fs, "filesystem tests"),
	SUITE(hush, "hush behaviour"),
	SUITE(image_fdt, "image fdt parsing"),
	SUITE(lib, "library functions"),
	SUITE(loadm, "loadm command parameters and loading memory blob"),
	SUITE(log, "logging functions"),
	SUITE(mbr, "mbr command"),
	SUITE(measurement, "TPM-based measured boot"),
	SUITE(mem, "memory-related commands"),
	SUITE(optee, "OP-TEE"),
	SUITE(pci_mps, "PCI Express Maximum Payload Size"),
	SUITE(pxe, "PXE/extlinux parser tests"),
	SUITE(seama, "seama command parameters loading and decoding"),
	SUITE(setexpr, "setexpr command"),
	SUITE(upl, "Universal payload support"),
};

/**
 * has_tests() - Check if a suite has tests, i.e. is supported in this build
 *
 * If the suite is run using a command, we have to assume that tests may be
 * present, since we have no visibility
 *
 * @ste: Suite to check
 * Return: true if supported, false if not
 */
static bool has_tests(struct suite *ste)
{
	int n_ents = ste->end - ste->start;

	return n_ents;
}

/** run_suite() - Run a suite of tests */
static int run_suite(struct unit_test_state *uts, struct suite *ste,
		     const char *select_name, int runs_per_test, bool force_run,
		     const char *test_insert, int argc, char *const argv[])
{
	int n_ents = ste->end - ste->start;
	char prefix[30];
	int ret;

	/* use a standard prefix */
	snprintf(prefix, sizeof(prefix), "%s_test_", ste->name);

	ret = ut_run_list(uts, ste->name, prefix, ste->start, n_ents,
			  select_name, runs_per_test, force_run, test_insert,
			  argc, argv);

	return ret;
}

static void show_stats(struct unit_test_state *uts)
{
	if (uts->run_count < 2)
		return;

	ut_report(&uts->total, uts->run_count);
	if (CONFIG_IS_ENABLED(UNIT_TEST_DURATION) &&
	    uts->total.test_count && uts->worst) {
		ulong avg = uts->total.duration_ms / uts->total.test_count;

		printf("Average test time: %ld ms, worst case '%s' took %d ms\n",
		       avg, uts->worst->name, uts->worst_ms);
	}
}

static void update_stats(struct unit_test_state *uts, const struct suite *ste)
{
	if (CONFIG_IS_ENABLED(UNIT_TEST_DURATION) && uts->cur.test_count) {
		ulong avg;

		avg = uts->cur.duration_ms ?
			uts->cur.duration_ms /
			uts->cur.test_count : 0;
		if (avg > uts->worst_ms) {
			uts->worst_ms = avg;
			uts->worst = ste;
		}
	}
}

static int do_ut_all(struct unit_test_state *uts, const char *select_name,
		     int runs_per_test, bool force_run, const char *test_insert,
		     int argc, char *const argv[])
{
	int i;
	int retval;
	int any_fail = 0;

	for (i = 0; i < ARRAY_SIZE(suites); i++) {
		struct suite *ste = &suites[i];

		if (has_tests(ste)) {
			printf("----Running %s tests----\n", ste->name);
			retval = run_suite(uts, ste, select_name, runs_per_test,
					   force_run, test_insert, argc, argv);
			if (!any_fail)
				any_fail = retval;
			update_stats(uts, ste);
		}
	}

	return any_fail;
}

static int do_ut_info(bool show_suites)
{
	int suite_count, i;

	for (suite_count = 0, i = 0; i < ARRAY_SIZE(suites); i++) {
		struct suite *ste = &suites[i];

		if (has_tests(ste))
			suite_count++;
	}

	printf("Test suites: %d\n", suite_count);
	printf("Total tests: %d\n", (int)UNIT_TEST_ALL_COUNT());

	if (show_suites) {
		int i, total;

		puts("\nTests  Suite         Purpose");
		puts("\n-----  ------------  -------------------------\n");
		for (i = 0, total = 0; i < ARRAY_SIZE(suites); i++) {
			struct suite *ste = &suites[i];
			long n_ent = ste->end - ste->start;

			if (n_ent) {
				printf("%5ld  %-13.13s %s\n", n_ent, ste->name,
				       ste->help);
				total += n_ent;
			}
		}
		puts("-----  ------------  -------------------------\n");
		printf("%5d  %-13.13s\n", total, "Total");

		if (UNIT_TEST_ALL_COUNT() != total)
			puts("Error: Suite test-count does not match total\n");
	}

	return 0;
}

static struct suite *find_suite(const char *name)
{
	struct suite *ste;
	int i;

	for (i = 0, ste = suites; i < ARRAY_SIZE(suites); i++, ste++) {
		if (!strcmp(ste->name, name))
			return ste;
	}

	return NULL;
}

static int do_ut(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	const char *test_insert = NULL, *select_name;
	int test_argc;
	char *const *test_argv;
	struct unit_test_state uts;
	bool show_suites = false;
	bool force_run = false;
	bool keep_record = false;
	bool emit_result = false;
	bool leak_check = false;
	int runs_per_text = 1;
	int workers = 0, worker_id = 0;
	struct suite *ste;
	char *name;
	int ret;

	/* drop initial "ut" arg */
	argc--;
	argv++;

	while (argc > 0 && *argv[0] == '-') {
		const char *str = argv[0];

		for (str++; *str; str++) {
			switch (*str) {
			case 'E':
				emit_result = true;
				break;
			case 'r':
				runs_per_text = dectoul(str + 1, NULL);
				goto next_arg;
			case 'f':
			case 'm':
				force_run = true;
				break;
			case 'L':
				leak_check = true;
				break;
			case 'I':
				test_insert = str + 1;
				if (!strchr(test_insert, ':'))
					return CMD_RET_USAGE;
				goto next_arg;
			case 'P': {
				const char *colon = strchr(str + 1, ':');

				if (!colon)
					return CMD_RET_USAGE;
				workers = dectoul(str + 1, NULL);
				worker_id = dectoul(colon + 1, NULL);
				goto next_arg;
			}
			case 'R':
				keep_record = true;
				break;
			case 's':
				show_suites = true;
				break;
			}
		}
next_arg:
		argv++;
		argc--;
	}

	if (argc < 1)
		return CMD_RET_USAGE;

	ut_init_state(&uts);
	uts.keep_record = keep_record;
	uts.emit_result = emit_result;
	uts.leak_check = leak_check;
	uts.workers = workers;
	uts.worker_id = worker_id;
	name = argv[0];
	select_name = cmd_arg1(argc, argv);

	/* Test arguments are after suite name and test name */
	test_argc = argc > 2 ? argc - 2 : 0;
	test_argv = argc > 2 ? argv + 2 : NULL;

	if (!strcmp(name, "all")) {
		ret = do_ut_all(&uts, select_name, runs_per_text, force_run,
				test_insert, test_argc, test_argv);
	} else if (!strcmp(name, "info")) {
		ret = do_ut_info(show_suites);
	} else {
		int any_fail = 0;
		const char *p;

		for (; p = strsep(&name, ","), p; name = NULL) {
			ste = find_suite(p);
			if (!ste) {
				printf("Suite '%s' not found\n", p);
				return CMD_RET_FAILURE;
			} else if (!has_tests(ste)) {
				/* perhaps a Kconfig option needs to be set? */
				printf("Suite '%s' is not enabled\n", p);
				return CMD_RET_FAILURE;
			}

			ret = run_suite(&uts, ste, select_name, runs_per_text,
					force_run, test_insert, test_argc,
					test_argv);
			if (!any_fail)
				any_fail = ret;
			update_stats(&uts, ste);
		}
		ret = any_fail;
	}
	show_stats(&uts);
	if (ret)
		return CMD_RET_FAILURE;
	ut_uninit_state(&uts);

	return 0;
}

U_BOOT_LONGHELP(ut,
	"[-ELfmrs] [-R] [-I<n>:<one_test>] [-P<n>:<w>] <suite> [<test> [<args>...]]\n"
	"                                                    - run unit tests\n"
	"   -E         Emit result line after each test\n"
	"   -L         Check for memory leaks around each test\n"
	"   -r<runs>   Number of times to run each test\n"
	"   -f/-m      Force 'manual' tests to run as well\n"
	"   -I         Test to run after <n> other tests have run\n"
	"   -P<n>:<w>  Run as worker <w> of <n> parallel workers\n"
	"   -R         Preserve console recording on test failure\n"
	"   -s         Show all suites with ut info\n"
	"   <suite>    Test suite to run (or comma-separated list)\n"
	"   <test>     Specific test to run (optional)\n"
	"   <args>     Test arguments as key=value pairs (optional)\n"
	"\n"
	"Options for <suite>:\n"
	"all       - execute all enabled tests\n"
	"info      - show info about tests [and suites]"
	);

U_BOOT_CMD(
	ut, CONFIG_SYS_MAXARGS, 1, do_ut,
	"unit tests", ut_help_text
);
