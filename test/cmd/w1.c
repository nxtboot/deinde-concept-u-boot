// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the w1 command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <w1.h>
#include <dm/uclass-internal.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the last line of the help text, which ends the usage message */
#define W1_USAGE_LAST \
	"      defaults: bus 0, dev 0, offset 0, length 512 bytes."

/**
 * check_no_bus() - Check that no 1-Wire bus is present
 *
 * These tests cover what the command does when there is no bus to talk to.
 * The test devicetree declares none, the 1-Wire node living in sandbox.dtsi,
 * so a board with a bus of its own must skip them.
 *
 * Return: 0 if there is no bus, -EAGAIN to skip the test if there is
 */
static int check_no_bus(void)
{
	struct udevice *dev;

	if (!uclass_find_first_device(UCLASS_W1, &dev) && dev)
		return -EAGAIN;

	return 0;
}

/**
 * check_usage() - Check the usage message, which a bad command line produces
 *
 * @uts: Test state
 * Return: 0 if OK, 1 on failure
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("w1 - onewire interface utility commands");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(W1_USAGE_LAST);
	ut_assert_console_end();

	return 0;
}

/* Test that the bus listing reports a bus which is not there */
static int cmd_test_w1_base(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_bus();
	if (ret)
		return ret;

	ut_asserteq(1, run_command("w1 bus", 0));
	ut_assert_nextline("one wire interface not found");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_w1_base, UTF_CONSOLE);

/* Test that a read reports the bus which is not there */
static int cmd_test_w1_read(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_bus();
	if (ret)
		return ret;

	/* every argument has a default, so the bus is looked for either way */
	ut_asserteq(1, run_command("w1 read", 0));
	ut_assert_nextline("one wire interface not found");
	ut_assert_console_end();

	ut_asserteq(1, run_command("w1 read 0 0 0 16", 0));
	ut_assert_nextline("one wire interface not found");
	ut_assert_console_end();

	/* a bus number past the end gives the same message */
	ut_asserteq(1, run_command("w1 read 1", 0));
	ut_assert_nextline("one wire interface not found");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_w1_read, UTF_CONSOLE);

/* Test that the length is limited to the size of the buffer */
static int cmd_test_w1_len(struct unit_test_state *uts)
{
	int ret;

	ret = check_no_bus();
	if (ret)
		return ret;

	/* the length is checked before the bus is looked for */
	ut_asserteq(1, run_command("w1 read 0 0 0 513", 0));
	ut_assert_nextline("len needs to be <= 512");
	ut_assert_console_end();

	/*
	 * A length above INT_MAX must be refused as well. It arrives negative
	 * where the length is held in an int, so the check passes it through
	 * and w1_read_buf() writes that many bytes into a 512-byte buffer
	 */
	ut_asserteq(1, run_command("w1 read 0 0 0 4294967295", 0));
	ut_assert_nextline("len needs to be <= 512");
	ut_assert_console_end();

	/* the largest length which fits is allowed through to the bus */
	ut_asserteq(1, run_command("w1 read 0 0 0 512", 0));
	ut_assert_nextline("one wire interface not found");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_w1_len, UTF_CONSOLE);

/* Test the command lines which the command refuses */
static int cmd_test_w1_usage(struct unit_test_state *uts)
{
	/* a sub-command is needed */
	ut_asserteq(1, run_command("w1", 0));
	ut_assertok(check_usage(uts));

	/* and it must be one the command knows */
	ut_asserteq(1, run_command("w1 bogus", 0));
	ut_assertok(check_usage(uts));

	/* a read takes at most four arguments */
	ut_asserteq(1, run_command("w1 read 0 0 0 16 0", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_w1_usage, UTF_CONSOLE);
