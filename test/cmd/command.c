// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2012, The Chromium Authors
 */

#define DEBUG

#include <command.h>
#include <env.h>
#include <getopt.h>
#include <log.h>
#include <string.h>
#include <linux/errno.h>
#include <test/cmd.h>
#include <test/ut.h>

static const char test_cmd[] = "setenv list 1\n setenv list ${list}2; "
		"setenv list ${list}3\0"
		"setenv list ${list}4";

static int command_test(struct unit_test_state *uts)
{
	char long_str[CONFIG_SYS_CBSIZE + 42];

	printf("%s: Testing commands\n", __func__);
	run_command("env default -f -a", 0);

	/* commands separated by \n */
	run_command_list("setenv list 1\n setenv list ${list}1", -1, 0);
	ut_assert(!strcmp("11", env_get("list")));

	/* command followed by \n and nothing else */
	run_command_list("setenv list 1${list}\n", -1, 0);
	ut_assert(!strcmp("111", env_get("list")));

	/* a command string with \0 in it. Stuff after \0 should be ignored */
	run_command("setenv list", 0);
	run_command_list(test_cmd, sizeof(test_cmd), 0);
	ut_assert(!strcmp("123", env_get("list")));

	/*
	 * a command list where we limit execution to only the first command
	 * using the length parameter.
	 */
	run_command_list("setenv list 1\n setenv list ${list}2; "
		"setenv list ${list}3", strlen("setenv list 1"), 0);
	ut_assert(!strcmp("1", env_get("list")));

	ut_assertok(run_command("echo", 0));
	ut_assertok(run_command_list("echo", -1, 0));

	if (IS_ENABLED(CONFIG_HUSH_PARSER)) {
		ut_asserteq(1, run_command("false", 0));
		ut_asserteq(1, run_command_list("false", -1, 0));
		run_command("setenv foo 'setenv black 1\nsetenv adder 2'", 0);
		run_command("run foo", 0);
		ut_assertnonnull(env_get("black"));
		ut_asserteq(0, strcmp("1", env_get("black")));
		ut_assertnonnull(env_get("adder"));
		ut_asserteq(0, strcmp("2", env_get("adder")));
		ut_assertok(run_command("", 0));
		ut_assertok(run_command(" ", 0));
	}

	ut_asserteq(1, run_command("'", 0));

	/* Variadic function test-cases */
	if (IS_ENABLED(CONFIG_HUSH_PARSER)) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-zero-length"
		ut_assertok(run_commandf(""));
#pragma GCC diagnostic pop
		ut_assertok(run_commandf(" "));
	}
	ut_asserteq(1, run_commandf("'"));

	ut_assertok(run_commandf("env %s %s", "delete -f", "list"));
	/*
	 * Expected: "## Error: "list" not defined"
	 * (disabled to avoid pytest bailing out)
	 *
	 * ut_asserteq(1, run_commandf("printenv list"));
	 */

	memset(long_str, 'x', sizeof(long_str));
	ut_asserteq(-ENOSPC, run_commandf("Truncation case: %s", long_str));

	if (IS_ENABLED(CONFIG_HUSH_PARSER)) {
		ut_assertok(run_commandf("env %s %s %s %s", "delete -f",
					 "adder", "black", "foo"));
		ut_assertok(run_commandf(
			"setenv foo 'setenv %s 1\nsetenv %s 2'",
			"black", "adder"));
		ut_assertok(run_command("run foo", 0));
		ut_assertnonnull(env_get("black"));
		ut_asserteq(0, strcmp("1", env_get("black")));
		ut_assertnonnull(env_get("adder"));
		ut_asserteq(0, strcmp("2", env_get("adder")));
	}

	/* Clean up before exit */
	ut_assertok(run_command("env default -f -a", 0));

	/* put back the FDT environment */
	ut_assertok(env_set("from_fdt", "yes"));

	printf("%s: Everything went swimmingly\n", __func__);
	return 0;
}
CMD_TEST(command_test, 0);

/* the arguments do_noopts() last saw, joined by spaces, or "" if not called */
static char noopts_seen[40];

/* Record the arguments, so a test can see whether and how it was called */
static int do_noopts(struct getopt_state *gs)
{
	const char *arg;

	strlcpy(noopts_seen, "called", sizeof(noopts_seen));
	while ((arg = getopt_pop(gs))) {
		strlcat(noopts_seen, " ", sizeof(noopts_seen));
		strlcat(noopts_seen, arg, sizeof(noopts_seen));
	}

	return 0;
}

static struct cmd_tbl noopts_cmd =
	U_BOOT_CMD_MKENT_NOOPTS(noopts, 4, 0, do_noopts, "", "");

static struct cmd_tbl getopt_cmd =
	U_BOOT_CMD_MKENT_GETOPT(getopt, 4, 0, do_noopts, "", "");

/**
 * invoke() - Run a test command through cmd_invoke()
 *
 * @cmdtp: Command to run
 * @argc: Number of arguments, including the command name
 * @argv: Arguments
 * Return: what cmd_invoke() returns
 */
static int invoke(struct cmd_tbl *cmdtp, int argc, char *const argv[])
{
	*noopts_seen = '\0';

	return cmd_invoke(cmdtp, 0, argc, argv);
}

/* Test that a command declared with no options refuses any */
static int command_test_noopts(struct unit_test_state *uts)
{
	char *const plain[] = { "noopts", "a", "b", NULL };
	char *const opt[] = { "noopts", "-x", "a", NULL };
	char *const late[] = { "noopts", "a", "-x", NULL };
	char *const dashes[] = { "noopts", "--", "-x", NULL };

	if (!IS_ENABLED(CONFIG_GETOPT))
		return -EAGAIN;

	/* the arguments reach the function untouched */
	ut_assertok(invoke(&noopts_cmd, 3, plain));
	ut_asserteq_str("called a b", noopts_seen);

	/* an option is refused before the function is called at all */
	ut_asserteq(CMD_RET_USAGE, invoke(&noopts_cmd, 3, opt));
	ut_asserteq_str("", noopts_seen);

	/* after the first argument, '-x' is an argument like any other */
	ut_assertok(invoke(&noopts_cmd, 3, late));
	ut_asserteq_str("called a -x", noopts_seen);

	/* and '--' ends the options, so it can come first too */
	ut_assertok(invoke(&noopts_cmd, 3, dashes));
	ut_asserteq_str("called -x", noopts_seen);

	/* the flag is what does it: the same function without it is called */
	ut_assertok(invoke(&getopt_cmd, 3, opt));
	ut_asserteq_str("called -x a", noopts_seen);

	return 0;
}
CMD_TEST(command_test_noopts, 0);
