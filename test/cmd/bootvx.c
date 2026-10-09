// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the bootvx command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * The command jumps to the image it is given, so these tests point it at a
 * function inside U-Boot itself, in the way the go tests do
 *
 * Every test carries the driver-model flags even though the command binds no
 * device. Setting 'bootargs' runs the on_bootargs() environment callback,
 * which probes bootstd, so the tests need a devicetree root which matches the
 * driver-model tree they are given.
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <malloc.h>
#include <mapmem.h>
#include <stdio.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Size of the buffer standing in for the VxWorks bootline area */
#define BOOTLINE_SIZE	256

/* Bootline the command builds from the variables the bootline test sets */
#define VX_NET		"e=192.0.2.1:ffffff00 h=192.0.2.2 g=192.0.2.3"
#define VX_BOOTLINE	"hosthost:vxWorks.bin " VX_NET " tn=sandbox o=1"

/* Variables the command reads when it works out where to boot from */
static const char *const vx_var[] = {
	"bootaddr", "bootargs", "bootdev", "bootfile", "ipaddr", "netmask",
	"serverip", "gatewayip", "hostname", "othbootargs",
};

/**
 * test_vx() - Stand-in for the VxWorks image which the command starts
 *
 * do_bootvx() jumps here with a single argument, as a VxWorks entry point
 * expects. Unlike a real image this one returns, so that the test can carry
 * on afterwards.
 *
 * @arg: Value the command passes, which is always 0
 */
static void test_vx(int arg)
{
	printf("vx: %d\n", arg);
}

/**
 * save_vars() - Take the bootline variables out of the way
 *
 * Sandbox sets some of these itself and another test may have set others, so
 * each test starts from nothing and puts in only what it needs.
 *
 * @uts: Test state
 * @saved: Returns a copy of each value, or NULL where it is unset
 */
static int save_vars(struct unit_test_state *uts, char **saved)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vx_var); i++) {
		const char *val = env_get(vx_var[i]);

		saved[i] = val ? strdup(val) : NULL;
		ut_assertok(env_set(vx_var[i], NULL));
	}

	return 0;
}

/**
 * restore_vars() - Put the bootline variables back as they were
 *
 * @uts: Test state
 * @saved: Value of each one before the test, as filled in by save_vars()
 */
static int restore_vars(struct unit_test_state *uts, char **saved)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(vx_var); i++) {
		ut_assertok(env_set(vx_var[i], saved[i]));
		free(saved[i]);
	}

	return 0;
}

/* Test 'bootvx' starting an image, with bootargs providing the bootline */
static int cmd_test_bootvx_base(struct unit_test_state *uts)
{
	char *saved[ARRAY_SIZE(vx_var)];
	ulong addr, bootaddr;
	char *buf;

	ut_assertok(save_vars(uts, saved));
	buf = malloc(BOOTLINE_SIZE);
	ut_assertnonnull(buf);
	bootaddr = map_to_sysmem(buf);
	ut_assertok(env_set_hex("bootaddr", bootaddr));
	ut_assertok(env_set("bootargs", "vxtest 1"));

	addr = map_to_sysmem(test_vx);
	ut_asserteq(1, run_commandf("bootvx %lx", addr));
	ut_assert_nextline("## Ethernet MAC address not copied to NV RAM");
	ut_assert_nextline("## Using bootline (@ 0x%lx): vxtest 1", bootaddr);
	ut_assert_nextline("## No elf image at address 0x%08lx", addr);
	ut_assert_nextline("## Not an ELF image, assuming binary");
	ut_assert_nextline("## Starting vxWorks at 0x%08lx ...", addr);
	ut_assert_nextline("vx: 0");
	ut_assert_nextline("## vxWorks terminated");
	ut_assert_console_end();

	/* the bootline must be where VxWorks would look for it */
	ut_asserteq_str("vxtest 1", buf);

	free(buf);
	ut_assertok(restore_vars(uts, saved));

	return 0;
}
CMD_TEST(cmd_test_bootvx_base, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test the bootline which the command builds for itself */
static int cmd_test_bootvx_bootline(struct unit_test_state *uts)
{
	const char *expect = VX_BOOTLINE;
	char *saved[ARRAY_SIZE(vx_var)];
	ulong addr, bootaddr;
	char *buf;

	ut_assertok(save_vars(uts, saved));
	buf = malloc(BOOTLINE_SIZE);
	ut_assertnonnull(buf);
	bootaddr = map_to_sysmem(buf);
	ut_assertok(env_set_hex("bootaddr", bootaddr));

	/* with bootargs unset, each field comes from a variable of its own */
	ut_assertok(env_set("bootdev", "host"));
	ut_assertok(env_set("bootfile", "vxWorks.bin"));
	ut_assertok(env_set("ipaddr", "192.0.2.1"));
	ut_assertok(env_set("netmask", "255.255.255.0"));
	ut_assertok(env_set("serverip", "192.0.2.2"));
	ut_assertok(env_set("gatewayip", "192.0.2.3"));
	ut_assertok(env_set("hostname", "sandbox"));
	ut_assertok(env_set("othbootargs", "o=1"));

	addr = map_to_sysmem(test_vx);
	ut_asserteq(1, run_commandf("bootvx %lx", addr));
	ut_assert_nextline("## Ethernet MAC address not copied to NV RAM");
	ut_assert_nextline("## Using bootline (@ 0x%lx): %s", bootaddr, expect);
	ut_assert_nextline("## No elf image at address 0x%08lx", addr);
	ut_assert_nextline("## Not an ELF image, assuming binary");
	ut_assert_nextline("## Starting vxWorks at 0x%08lx ...", addr);
	ut_assert_nextline("vx: 0");
	ut_assert_nextline("## vxWorks terminated");
	ut_assert_console_end();

	ut_asserteq_str(expect, buf);

	free(buf);
	ut_assertok(restore_vars(uts, saved));

	return 0;
}
CMD_TEST(cmd_test_bootvx_bootline, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'bootvx' with no boot device to put in the bootline */
static int cmd_test_bootvx_nodev(struct unit_test_state *uts)
{
	char *saved[ARRAY_SIZE(vx_var)];
	ulong addr, bootaddr;
	char *buf;

	ut_assertok(save_vars(uts, saved));
	buf = malloc(BOOTLINE_SIZE);
	ut_assertnonnull(buf);
	bootaddr = map_to_sysmem(buf);
	ut_assertok(env_set_hex("bootaddr", bootaddr));

	/* the command says so and carries on, falling back to 'host:vxWorks' */
	addr = map_to_sysmem(test_vx);
	ut_asserteq(1, run_commandf("bootvx %lx", addr));
	ut_assert_nextline("## Ethernet MAC address not copied to NV RAM");
	ut_assert_nextline("## VxWorks boot device not specified");
	ut_assert_nextline("## Using bootline (@ 0x%lx): host:vxWorks ",
			   bootaddr);
	ut_assert_skip_to_line("## vxWorks terminated");
	ut_assert_console_end();

	free(buf);
	ut_assertok(restore_vars(uts, saved));

	return 0;
}
CMD_TEST(cmd_test_bootvx_nodev, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'bootvx' with nowhere to put the bootline */
static int cmd_test_bootvx_noaddr(struct unit_test_state *uts)
{
	char *saved[ARRAY_SIZE(vx_var)];
	ulong addr;

	ut_assertok(save_vars(uts, saved));

	/* without bootaddr the command has nowhere to write, so it gives up */
	addr = map_to_sysmem(test_vx);
	ut_asserteq(1, run_commandf("bootvx %lx", addr));
	ut_assert_nextline("## Ethernet MAC address not copied to NV RAM");
	ut_assert_nextline("## VxWorks bootline address not specified");
	ut_assert_console_end();

	ut_assertok(restore_vars(uts, saved));

	return 0;
}
CMD_TEST(cmd_test_bootvx_noaddr, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test 'bootvx tftp' with no server to fetch the image from */
static int cmd_test_bootvx_tftp(struct unit_test_state *uts)
{
	char *saved[ARRAY_SIZE(vx_var)];

	if (!IS_ENABLED(CONFIG_CMD_NET) || IS_ENABLED(CONFIG_NET_LWIP))
		return -EAGAIN;

	ut_assertok(save_vars(uts, saved));

	/* the transfer is attempted before anything else, and fails here */
	ut_asserteq(1, run_command("bootvx tftp", 0));
	ut_assert_nextline("*** ERROR: `serverip' not set");
	ut_assert_console_end();

	ut_assertok(restore_vars(uts, saved));

	return 0;
}
CMD_TEST(cmd_test_bootvx_tftp, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test that the command refuses an option, since it has none */
static int cmd_test_bootvx_opt(struct unit_test_state *uts)
{
	const char *help = " [address] - load address of vxWorks ELF image.";

	ut_asserteq(1, run_command("bootvx -x", 0));
	ut_assert_nextline("bootvx - Boot vxWorks from an ELF image");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("bootvx %s", help);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bootvx_opt, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);
