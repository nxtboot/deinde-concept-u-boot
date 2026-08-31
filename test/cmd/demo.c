// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the demo command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <dm-demo.h>
#include <errno.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * The demo devices come from platform data, so the tests need a tree of their
 * own with that data scanned into it. Without UTF_DM they run against whatever
 * tree the tests before them leave behind, in which a device with no
 * devicetree node of its own is taken to have one
 */
#define DEMO_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA)

/* Devices bound from platform data: 0, 2 and 4 are shapes, 1 and 3 simple */
#define DEMO_SHAPE	0
#define DEMO_SIMPLE	1

/* Shape device used only by the status test, so that its count is its own */
#define DEMO_STATUS	4

/* Number of characters that device draws, its shape being a hexagon */
#define DEMO_CHARS	36

/* Device which is not there, since sandbox binds five */
#define DEMO_MISSING	9

/*
 * The last line of the help text. It ends with a newline of its own, so the
 * usage message finishes with an empty line
 */
#define DEMO_USAGE_LAST	"demo status <num>             Get demo device status"

/**
 * check_shape() - Check the shape drawn by the red square
 *
 * @uts: Test state
 * @ch: Character the shape is drawn with
 * Return: 0 if OK, -ve on error
 */
static int check_shape(struct unit_test_state *uts, char ch)
{
	const char *colour = "red";
	char line[9];
	int i;

	memset(line, ch, sizeof(line) - 1);
	line[sizeof(line) - 1] = '\0';

	/*
	 * The four-sided shape fills every line, with the colour down the
	 * left-hand side, one letter at a time
	 */
	for (i = 0; i < 6; i++) {
		line[0] = colour[i % strlen(colour)];
		ut_assert_nextline("%s", line);
	}
	ut_assert_console_end();

	return 0;
}

/* Test 'demo list' */
static int cmd_test_demo_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("demo list", 0));
	ut_assert_nextline("Demo uclass entries:");

	/*
	 * The addresses vary, but every device must be there and must have
	 * probed, which is the status at the end of the line
	 */
	ut_assert_nextlinen("entry 0 - instance ");
	ut_assert_nextlinen("entry 1 - instance ");
	ut_assert_nextlinen("entry 2 - instance ");
	ut_assert_nextlinen("entry 3 - instance ");
	ut_assert_nextlinen("entry 4 - instance ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_base, DEMO_TEST_FLAGS);

/* Test 'demo hello' on both drivers */
static int cmd_test_demo_hello(struct unit_test_state *uts)
{
	ut_assertok(run_commandf("demo hello %d", DEMO_SHAPE));
	ut_assertok(check_shape(uts, '@'));

	/* the character to draw with defaults to the one in the plat data */
	ut_assertok(run_commandf("demo hello %d x", DEMO_SHAPE));
	ut_assertok(check_shape(uts, 'x'));

	/* the simple driver has nothing to draw, so it says so instead */
	ut_assertok(run_commandf("demo hello %d", DEMO_SIMPLE));
	ut_assert_nextlinen("Hello from ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_hello, DEMO_TEST_FLAGS);

/* Test 'demo status', which counts what 'demo hello' draws */
static int cmd_test_demo_status(struct unit_test_state *uts)
{
	/*
	 * Use a device of its own, since the count is kept in the device and
	 * anything drawn by another test adds to it
	 */
	ut_assertok(run_commandf("demo status %d", DEMO_STATUS));
	ut_assert_nextline("Status: 0");
	ut_assert_console_end();

	/* the shape itself is checked elsewhere, so throw it away here */
	ut_assertok(run_commandf("demo hello %d", DEMO_STATUS));
	console_record_reset();

	ut_assertok(run_commandf("demo status %d", DEMO_STATUS));
	ut_assert_nextline("Status: %d", DEMO_CHARS);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_status, DEMO_TEST_FLAGS);

/* Test 'demo light' */
static int cmd_test_demo_light(struct unit_test_state *uts)
{
	ut_assertok(run_commandf("demo light %d", DEMO_SHAPE));
	ut_assert_nextline("Light: 0");
	ut_assert_console_end();

	/*
	 * The devices bound from platform data have no light GPIOs, so setting
	 * the lights succeeds but changes nothing
	 */
	ut_assertok(run_commandf("demo light %d 3", DEMO_SHAPE));
	ut_assert_console_end();

	ut_assertok(run_commandf("demo light %d", DEMO_SHAPE));
	ut_assert_nextline("Light: 0");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_light, DEMO_TEST_FLAGS);

/* Test the operations the simple driver does not implement */
static int cmd_test_demo_unsupported(struct unit_test_state *uts)
{
	ut_asserteq(1, run_commandf("demo status %d", DEMO_SIMPLE));
	ut_assert_nextline("Command 'status' failed: Error %d", -ENOSYS);
	ut_assert_console_end();

	ut_asserteq(1, run_commandf("demo light %d", DEMO_SIMPLE));
	ut_assert_nextline("Command 'light' failed: Error %d", -ENOSYS);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_unsupported, DEMO_TEST_FLAGS);

/* Test a device which is not there */
static int cmd_test_demo_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_commandf("demo hello %d", DEMO_MISSING));
	ut_assert_nextline("Command 'hello' failed: Error %d", -ENODEV);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_missing, DEMO_TEST_FLAGS);

/* Test the usage errors */
static int cmd_test_demo_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("demo", 0));
	ut_assert_skip_to_line(DEMO_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	ut_asserteq(1, run_command("demo fish 0", 0));
	ut_assert_skip_to_line(DEMO_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* every sub-command but 'demo list' needs a device number */
	ut_asserteq(1, run_command("demo hello", 0));
	ut_assert_skip_to_line(DEMO_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* 'demo list' takes none */
	ut_asserteq(1, run_command("demo list 0", 0));
	ut_assert_skip_to_line(DEMO_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_demo_usage, DEMO_TEST_FLAGS);
