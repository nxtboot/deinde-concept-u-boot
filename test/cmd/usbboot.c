// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the usbboot command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the load address the tests would read into */
#define USBBOOT_ADDR	"1000"

/*
 * These tests name no storage device on purpose. Sandbox emulates four sticks
 * but 'usb reset' renumbers them, so device 0 is a different stick after every
 * reset, and the sticks which have a backing file get their contents from the
 * test setup. What the command does before it reaches a device is the same
 * every time, which is what is checked here.
 */

/* Test the device specifications which name nothing to boot from */
static int cmd_test_usbboot_missing(struct unit_test_state *uts)
{
	/* the default device comes from the environment, which is unset */
	ut_asserteq(1, run_command("usbboot", 0));
	ut_assert_nextline("** No device specified **");
	ut_assert_console_end();

	/* an address on its own leaves the device just as unspecified */
	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR, 0));
	ut_assert_nextline("** No device specified **");
	ut_assert_console_end();

	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR " 99:1", 0));
	ut_assert_nextline("** Bad device specification usb 99 **");
	ut_assert_console_end();

	/* a '-' means the same as leaving the argument out */
	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR " -", 0));
	ut_assert_nextline("** No device specified **");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_usbboot_missing, UTF_CONSOLE);

/* Test that the device defaults to the bootdevice environment variable */
static int cmd_test_usbboot_bootdevice(struct unit_test_state *uts)
{
	ut_assertok(run_command("setenv bootdevice 99:1", 0));

	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR, 0));
	ut_assert_nextline("** Bad device specification usb 99 **");
	ut_assert_console_end();

	/* the command line wins where both are given */
	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR " 98:1", 0));
	ut_assert_nextline("** Bad device specification usb 98 **");
	ut_assert_console_end();

	ut_assertok(run_command("setenv bootdevice", 0));

	return 0;
}
CMD_TEST(cmd_test_usbboot_bootdevice, UTF_CONSOLE);

/* Test the command lines which the command refuses */
static int cmd_test_usbboot_usage(struct unit_test_state *uts)
{
	/* the command takes two arguments at most */
	ut_asserteq(1, run_command("usbboot " USBBOOT_ADDR " 0:0 1", 0));
	ut_assert_nextline("usbboot - boot from USB device");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("usbboot loadAddr dev:part");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_usbboot_usage, UTF_CONSOLE);
