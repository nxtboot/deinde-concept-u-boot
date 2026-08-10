// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the eraseenv command
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <env.h>
#include <os.h>
#include <asm/state.h>
#include <asm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

/**
 * env_test_fname() - Work out a filename to hold the test environment
 *
 * This sits beside the sandbox binary, so that the sandbox variants running
 * at the same time do not share a file.
 *
 * @buf: Buffer to hold the name
 * @size: Size of buffer
 * Return: 0 if OK, -ENOENT if the program name is not known
 */
static int env_test_fname(char *buf, int size)
{
	struct sandbox_state *state = state_get_current();
	const char *prog, *slash;

	prog = state->prog_fname ? state->prog_fname : state->argv[0];
	if (!prog)
		return -ENOENT;

	slash = strrchr(prog, '/');
	if (slash)
		snprintf(buf, size, "%.*s/uboot-test.env",
			 (int)(slash - prog), prog);
	else
		snprintf(buf, size, "uboot-test.env");

	return 0;
}

/* Test 'eraseenv' when there is nowhere to erase */
static int cmd_test_eraseenv_nowhere(struct unit_test_state *uts)
{
	/* the default location cannot erase anything */
	ut_asserteq(1, run_command("eraseenv", 0));
	ut_assert_nextline("not possible");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_eraseenv_nowhere, UTF_CONSOLE);

/* Test 'eraseenv' with a persistent environment to erase */
static int cmd_test_eraseenv_base(struct unit_test_state *uts)
{
	char fname[256];
	int fd;

	ut_assertok(env_test_fname(fname, sizeof(fname)));
	os_unlink(fname);

	/* take over the environment, so that there is something to erase */
	ut_assertok(sandbox_env_set_file(fname));
	console_record_reset();

	ut_assertok(env_set("ut_erase", "gravel"));
	ut_assertok(run_command("saveenv", 0));
	ut_assert_nextlinen("Saving Environment to sandbox");
	ut_assert_console_end();

	/* the file now exists */
	fd = os_open(fname, OS_O_RDONLY);
	ut_assert(fd >= 0);
	os_close(fd);

	ut_assertok(run_command("eraseenv", 0));
	ut_assert_nextlinen("Erasing Environment on sandbox");
	ut_assert_console_end();

	/* erasing removes the file, so there is nothing left to load */
	ut_assert(os_open(fname, OS_O_RDONLY) < 0);

	/* put the previous location back for whatever runs next */
	ut_assertok(sandbox_env_set_file(NULL));

	return 0;
}
CMD_TEST(cmd_test_eraseenv_base, UTF_CONSOLE);
