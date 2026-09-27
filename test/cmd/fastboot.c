// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the fastboot command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <fastboot.h>
#include <fastboot-internal.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Ethernet device the UDP transport is told to listen on */
#define FB_ETHACT	"eth@10002000"

/* the address sandbox gives that device, which the server reports */
#define FB_IPADDR	"192.0.2.1"

/*
 * The names of the functions which log the transport messages, right-aligned
 * in CONFIG_LOGF_FUNC_PAD (20) characters as the log emits them
 */
#define FB_FUNC		"         do_fastboot"
#define FB_FUNC_USB	"     do_fastboot_usb"
#define FB_FUNC_TCP	"     do_fastboot_tcp"

/* download buffer the -l and -s options ask for */
#define FB_BUF_ADDR	0x1000
#define FB_BUF_SIZE	0x200

/**
 * check_log() - Check the next console line, which a transport logs
 *
 * The log format shows the function name only when CONFIG_LOGF_FUNC is
 * enabled. That is true of sandbox but of none of the other sandbox
 * variants, so the expected line differs between them.
 *
 * @uts: Test state
 * @func: Name of the function which emits the message, padded as the log pads
 *	it
 * @msg: Rest of the line
 * Return: 0 if OK, 1 on failure
 */
static int check_log(struct unit_test_state *uts, const char *func,
		     const char *msg)
{
	if (IS_ENABLED(CONFIG_LOGF_FUNC))
		ut_assert_nextline("%s() %s", func, msg);
	else
		ut_assert_nextline("%s", msg);

	return 0;
}

/**
 * check_usage() - Check the usage message the command prints
 *
 * @uts: Test state
 * Return: 0 if OK, 1 on failure
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("fastboot - run as a fastboot usb or udp device");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("fastboot [-l addr] [-s size] usb <controller> | udp");
	ut_assert_skip_to_linen("\tsize - size of buffer");
	ut_assert_console_end();

	return 0;
}

/* A transport is required, and an option is not one */
static int cmd_test_fastboot_base(struct unit_test_state *uts)
{
	/* nothing at all */
	ut_asserteq(1, run_command("fastboot", 0));
	ut_assertok(check_usage(uts));

	/* an option with no argument leaves nothing to dispatch on */
	ut_asserteq(1, run_command("fastboot -l", 0));
	ut_assertok(check_usage(uts));

	/* nor does a lone dash, which the command reports before the usage */
	ut_asserteq(1, run_command("fastboot -", 0));
	ut_assertok(check_log(uts, FB_FUNC,
			      "Error: Incorrect USB controller index"));
	ut_assertok(check_usage(uts));

	/* an option the command does not have is refused */
	ut_asserteq(1, run_command("fastboot -q udp", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_fastboot_base, UTF_CONSOLE);

/* The USB transport is not built into sandbox, so it says so */
static int cmd_test_fastboot_usb(struct unit_test_state *uts)
{
	/* a board with the gadget starts it instead of reporting this */
	if (IS_ENABLED(CONFIG_USB_FUNCTION_FASTBOOT))
		return -EAGAIN;

	ut_asserteq(1, run_command("fastboot usb 0", 0));
	ut_assertok(check_log(uts, FB_FUNC_USB, "Fastboot USB not enabled"));
	ut_assert_console_end();

	/* the usb word is optional, since the transport defaults to it */
	ut_asserteq(1, run_command("fastboot 0", 0));
	ut_assertok(check_log(uts, FB_FUNC_USB, "Fastboot USB not enabled"));
	ut_assert_console_end();

	/*
	 * the controller number is checked only once the gadget is there, so
	 * leaving it out reports the same thing here
	 */
	ut_asserteq(1, run_command("fastboot usb", 0));
	ut_assertok(check_log(uts, FB_FUNC_USB, "Fastboot USB not enabled"));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_fastboot_usb, UTF_CONSOLE);

/* Nor is the TCP transport */
static int cmd_test_fastboot_tcp(struct unit_test_state *uts)
{
	if (IS_ENABLED(CONFIG_TCP_FUNCTION_FASTBOOT))
		return -EAGAIN;

	ut_asserteq(1, run_command("fastboot tcp", 0));
	ut_assertok(check_log(uts, FB_FUNC_TCP, "Fastboot TCP not enabled"));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_fastboot_tcp, UTF_CONSOLE);

/* The -l and -s options set the download buffer for the session */
static int cmd_test_fastboot_buf(struct unit_test_state *uts)
{
	if (IS_ENABLED(CONFIG_TCP_FUNCTION_FASTBOOT))
		return -EAGAIN;

	/*
	 * the buffer is set up before the transport is dispatched to, so a
	 * transport which is not built in still shows what the options did
	 */
	ut_asserteq(1, run_commandf("fastboot -l %x -s %x tcp", FB_BUF_ADDR,
				    FB_BUF_SIZE));
	ut_assertok(check_log(uts, FB_FUNC_TCP, "Fastboot TCP not enabled"));
	ut_assert_console_end();
	ut_asserteq_ptr((void *)FB_BUF_ADDR, fastboot_buf_addr);
	ut_asserteq(FB_BUF_SIZE, fastboot_buf_size);

	/* without them the configured defaults come back */
	ut_asserteq(1, run_command("fastboot tcp", 0));
	ut_assertok(check_log(uts, FB_FUNC_TCP, "Fastboot TCP not enabled"));
	ut_assert_console_end();
	ut_asserteq_ptr((void *)CONFIG_FASTBOOT_BUF_ADDR, fastboot_buf_addr);
	ut_asserteq(CONFIG_FASTBOOT_BUF_SIZE, fastboot_buf_size);

	return 0;
}
CMD_TEST(cmd_test_fastboot_buf, UTF_CONSOLE);

/* The UDP transport starts a server and waits for a host */
static int cmd_test_fastboot_udp(struct unit_test_state *uts)
{
	int prev;

	if (!IS_ENABLED(CONFIG_UDP_FUNCTION_FASTBOOT))
		return -EAGAIN;

	ut_assertok(env_set("ethact", FB_ETHACT));

	/*
	 * The server waits for a host which never arrives, so queue a Ctrl-C
	 * before starting it. Sandbox turns off Ctrl-C checking, so enable it
	 * for the duration of the command
	 */
	console_in_puts("\x03");
	prev = disable_ctrlc(0);
	ut_asserteq(1, run_command("fastboot udp", 0));
	disable_ctrlc(prev);

	ut_assert_nextline("Using " FB_ETHACT " device");
	ut_assert_nextline("Listening for fastboot command on " FB_IPADDR);
	ut_assert_nextline_empty();
	ut_assert_nextline("Abort");

	/* net_loop() gives -EINTR, which the command reports as it stands */
	ut_assert_nextline("fastboot udp error: %d", -EINTR);
	ut_assert_console_end();
	ut_assertok(env_set("ethact", NULL));

	return 0;
}
CMD_TEST(cmd_test_fastboot_udp, UTF_CONSOLE);
