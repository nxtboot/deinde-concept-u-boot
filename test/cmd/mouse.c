// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mouse command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <mouse.h>
#include <asm/test.h>
#include <test/cmd.h>
#include <test/ut.h>

/**
 * get_mouse() - Find the mouse and put it in test mode
 *
 * @uts: Test state
 * @devp: Returns the mouse device
 * Return: 0 if OK, -ve on error
 */
static int get_mouse(struct unit_test_state *uts, struct udevice **devp)
{
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_MOUSE, &dev));
	sandbox_mouse_set_test_mode(dev, true);
	*devp = dev;

	return 0;
}

/**
 * run_dump() - Run 'mouse dump' and let it finish
 *
 * The command runs until Ctrl-C is pressed, so queue one up before starting
 * it. Sandbox turns off Ctrl-C checking, so enable it for the duration.
 *
 * @uts: Test state
 * Return: 0 if OK, -ve on error
 */
static int run_dump(struct unit_test_state *uts)
{
	int prev;

	ut_asserteq(1, console_in_puts("\x03"));
	prev = disable_ctrlc(0);
	ut_assertok(run_command("mouse dump", 0));
	disable_ctrlc(prev);

	return 0;
}

/* Test 'mouse dump' with no events waiting */
static int cmd_test_mouse_base(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(get_mouse(uts, &dev));

	ut_assertok(run_dump(uts));
	ut_assert_nextline("0 events received");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mouse_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'mouse dump' showing a motion event */
static int cmd_test_mouse_motion(struct unit_test_state *uts)
{
	struct mouse_event inject;
	struct udevice *dev;

	ut_assertok(get_mouse(uts, &dev));

	inject.type = MOUSE_EV_MOTION;
	inject.motion.state = BUTTON_LEFT;
	inject.motion.x = 100;
	inject.motion.y = 200;
	inject.motion.xrel = 10;
	inject.motion.yrel = -20;
	sandbox_mouse_inject(dev, &inject);

	ut_assertok(run_dump(uts));
	ut_assert_nextline("motion: Xrel=10, Yrel=-20, X=100, Y=200, but=1");
	ut_assert_nextline("1 events received");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mouse_motion, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'mouse dump' showing a button event */
static int cmd_test_mouse_button(struct unit_test_state *uts)
{
	struct mouse_event inject;
	struct udevice *dev;

	ut_assertok(get_mouse(uts, &dev));

	inject.type = MOUSE_EV_BUTTON;
	inject.button.button = BUTTON_RIGHT;
	inject.button.pressed = true;
	inject.button.clicks = 2;
	inject.button.x = 150;
	inject.button.y = 250;
	sandbox_mouse_inject(dev, &inject);

	ut_assertok(run_dump(uts));
	ut_assert_nextline("button: button==4, press=1, clicks=2, X=150, Y=250");
	ut_assert_nextline("1 events received");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mouse_button, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'mouse' with a bad command line */
static int cmd_test_mouse_usage(struct unit_test_state *uts)
{
	int i;

	/* the sub-command is required and must be one the command knows */
	for (i = 0; i < 2; i++) {
		ut_asserteq(1, run_command(i ? "mouse bogus" : "mouse", 0));
		ut_assert_nextline("mouse - Mouse input");
		ut_assert_nextline_empty();
		ut_assert_nextline("Usage:");
		ut_assert_nextline("mouse dump - Dump input from a mouse");
		ut_assert_console_end();
	}

	return 0;
}
CMD_TEST(cmd_test_mouse_usage, UTF_CONSOLE);
