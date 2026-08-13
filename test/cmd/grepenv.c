// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the grepenv command
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <env.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Set up two variables which no other test uses */
static int setup_vars(struct unit_test_state *uts)
{
	ut_assertok(env_set("ut_grep_one", "ut_alpha"));
	ut_assertok(env_set("ut_grep_two", "ut_beta"));

	return 0;
}

/* Test 'grepenv' searching names and values */
static int cmd_test_grepenv_base(struct unit_test_state *uts)
{
	ut_assertok(setup_vars(uts));

	/* a substring of the name matches, with the value shown after it */
	ut_assertok(run_command("grepenv ut_grep_one", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_console_end();

	/* a substring of the value matches too, since -b is the default */
	ut_assertok(run_command("grepenv ut_beta", 0));
	ut_assert_nextline("ut_grep_two=ut_beta");
	ut_assert_console_end();

	/* several names can match, in which case they are sorted */
	ut_assertok(run_command("grepenv ut_grep_", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_nextline("ut_grep_two=ut_beta");
	ut_assert_console_end();

	/* more than one search string can be given */
	ut_assertok(run_command("grepenv ut_alpha ut_beta", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_nextline("ut_grep_two=ut_beta");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_grepenv_base, UTF_CONSOLE);

/* Test the 'grepenv' flags which limit what is searched */
static int cmd_test_grepenv_flags(struct unit_test_state *uts)
{
	ut_assertok(setup_vars(uts));

	/* -n searches names only, so a value does not match */
	ut_assertok(run_command("grepenv -n ut_grep_one", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_console_end();
	ut_asserteq(1, run_command("grepenv -n ut_alpha", 0));
	ut_assert_console_end();

	/* -v searches values only, so a name does not match */
	ut_assertok(run_command("grepenv -v ut_alpha", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_console_end();
	ut_asserteq(1, run_command("grepenv -v ut_grep_one", 0));
	ut_assert_console_end();

	/* -b searches both, which is what happens with no flag */
	ut_assertok(run_command("grepenv -b ut_alpha", 0));
	ut_assert_nextline("ut_grep_one=ut_alpha");
	ut_assert_console_end();

	if (IS_ENABLED(CONFIG_REGEX)) {
		/* -e treats the string as a regular expression */
		ut_assertok(run_command("grepenv -e -n ^ut_grep_t", 0));
		ut_assert_nextline("ut_grep_two=ut_beta");
		ut_assert_console_end();

		/* without -e the same string is just a substring */
		ut_asserteq(1, run_command("grepenv -n ^ut_grep_t", 0));
		ut_assert_console_end();
	}

	return 0;
}
CMD_TEST(cmd_test_grepenv_flags, UTF_CONSOLE);

/* Test 'grepenv' when it finds nothing or is used wrongly */
static int cmd_test_grepenv_none(struct unit_test_state *uts)
{
	ut_assertok(setup_vars(uts));

	/* a string which matches nothing prints nothing and fails */
	ut_asserteq(1, run_command("grepenv ut_no_such_string", 0));
	ut_assert_console_end();

	/* so does an unknown flag, except that it shows the usage */
	ut_asserteq(1, run_command("grepenv -z ut_grep_one", 0));
	ut_assert_nextlinen("grepenv - search environment variables");
	ut_assert_nextline_empty();
	ut_assert_nextlinen("Usage:");
	ut_assert_skip_to_linen("      \"-b\"");
	ut_assert_console_end();

	/* with no search string at all there is nothing to look for */
	ut_asserteq(1, run_command("grepenv", 0));
	ut_assert_nextlinen("grepenv - search environment variables");
	ut_assert_skip_to_linen("      \"-b\"");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_grepenv_none, UTF_CONSOLE);
