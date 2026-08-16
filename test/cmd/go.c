// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the go command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * These tests point the command at a function inside U-Boot itself, since
 * sandbox has no separate application to start
 */

#include <command.h>
#include <console.h>
#include <stdio.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Value which the stand-in application returns; set before each test */
static ulong app_result;

/**
 * test_app() - Stand-in for the application which 'go' starts
 *
 * The command jumps straight to the address it is given, so this has the
 * signature which do_go_exec() calls
 *
 * @argc: Number of arguments, the first being the address itself
 * @argv: Arguments, with the address as argv[0]
 * Return: value chosen by the test
 */
static ulong test_app(int argc, char *const argv[])
{
	int i;

	printf("app:");
	for (i = 0; i < argc; i++)
		printf(" %s", argv[i]);
	printf("\n");

	return app_result;
}

/* Test 'go' starting an application */
static int cmd_test_go_base(struct unit_test_state *uts)
{
	ulong addr = (ulong)test_app;

	app_result = 0;

	/*
	 * The address is passed on as argv[0], in the same form as it was
	 * typed, with the remaining arguments after it
	 */
	ut_assertok(run_commandf("go %lx hello world", addr));
	ut_assert_nextline("## Starting application at 0x%08lX ...", addr);
	ut_assert_nextline("app: %lx hello world", addr);
	ut_assert_nextline("## Application terminated, rc = 0x0");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_go_base, UTF_CONSOLE);

/* Test 'go' with no arguments for the application */
static int cmd_test_go_noargs(struct unit_test_state *uts)
{
	ulong addr = (ulong)test_app;

	app_result = 0;

	/* the address is still passed as argv[0], so argc is 1, not 0 */
	ut_assertok(run_commandf("go %lx", addr));
	ut_assert_nextline("## Starting application at 0x%08lX ...", addr);
	ut_assert_nextline("app: %lx", addr);
	ut_assert_nextline("## Application terminated, rc = 0x0");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_go_noargs, UTF_CONSOLE);

/* Test 'go' with an application which fails */
static int cmd_test_go_fail(struct unit_test_state *uts)
{
	ulong addr = (ulong)test_app;

	app_result = 0x1234;

	/*
	 * Whatever the application returns is printed, but the command itself
	 * reports only success or failure
	 */
	ut_asserteq(1, run_commandf("go %lx", addr));
	ut_assert_nextline("## Starting application at 0x%08lX ...", addr);
	ut_assert_nextline("app: %lx", addr);
	ut_assert_nextline("## Application terminated, rc = 0x1234");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_go_fail, UTF_CONSOLE);

/* Test 'go' with a bad command line */
static int cmd_test_go_usage(struct unit_test_state *uts)
{
	/* the address is required */
	ut_asserteq(1, run_command("go", 0));
	ut_assert_nextline("go - start application at address 'addr'");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("go addr [arg ...]");
	ut_assert_nextline("    - start application at address 'addr'");
	ut_assert_nextline("      passing 'arg' as arguments");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_go_usage, UTF_CONSOLE);
