// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the I/O space commands
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <pci.h>
#include <asm/io.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * These tests reach the I/O space through the sandbox PCI emulation, so they
 * need a driver-model state of their own, as the driver-model tests do
 */
#define IO_TEST(_name)							\
	CMD_TEST(_name, UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/*
 * get_io_addr() - Find the I/O address of the sandbox swap-case emulator
 *
 * Looking up the device enumerates the bus, which is what assigns the base
 * address of its one-byte I/O region
 *
 * @uts: Test state
 * @addrp: Returns the I/O address
 * Return: 0 if OK, other value on error
 */
static int get_io_addr(struct unit_test_state *uts, ulong *addrp)
{
	struct udevice *swap;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x00, 0), &swap));
	*addrp = dm_pci_read_bar32(swap, 0);
	ut_assert(*addrp);

	return 0;
}

/* Test 'iod' displaying a byte of I/O space */
static int cmd_test_iod_base(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));

	/* the emulator hands back the value last written to its I/O region */
	outb(2, io_addr);
	ut_assertok(run_commandf("iod.b %lx 1", io_addr));
	ut_assert_nextlinen("%08lx: 02", io_addr);
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iod_base);

/* Test that 'iod' reads with a single access of the requested size */
static int cmd_test_iod_size(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	outb(3, io_addr);

	/*
	 * the emulator implements only the first byte of its region, so the
	 * bytes above it read back as all-ones, which is what a PCI bus
	 * returns when nothing responds
	 */
	ut_assertok(run_commandf("iod.w %lx 1", io_addr));
	ut_assert_nextlinen("%08lx: ff03", io_addr);

	ut_assertok(run_commandf("iod %lx 1", io_addr));
	ut_assert_nextlinen("%08lx: ffffff03", io_addr);
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iod_size);

/* Test that 'iod' shows a run of values, one line at a time */
static int cmd_test_iod_length(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	outb(4, io_addr);

	/*
	 * the length is a count of values, so this asks for 0x14 bytes, which
	 * needs two lines. Only the first byte is backed by the emulator; a
	 * byte read which nothing answers comes back as zero
	 */
	ut_assertok(run_commandf("iod.b %lx 14", io_addr));
	ut_assert_nextlinen("%08lx: 04 00 00 00", io_addr);
	ut_assert_nextlinen("%08lx: 00 00 00 00", io_addr + 0x10);
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iod_length);

/* Test that repeating 'iod' carries on where the last one stopped */
static int cmd_test_iod_repeat(struct unit_test_state *uts)
{
	char *const args[] = { "iod.b", NULL, "10", NULL };
	char addr_str[20];
	int repeatable;
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	outb(5, io_addr);

	snprintf(addr_str, sizeof(addr_str), "%lx", io_addr);
	((char **)args)[1] = addr_str;

	/* a fresh command starts at the address it is given */
	ut_assertok(cmd_process(0, 3, args, &repeatable, NULL));
	ut_assert_nextlinen("%08lx: 05", io_addr);
	ut_assert_console_end();

	/*
	 * A repeat takes no notice of the arguments and shows the next block,
	 * which it can only do by seeing CMD_FLAG_REPEAT in the state
	 */
	ut_assertok(cmd_process(CMD_FLAG_REPEAT, 3, args, &repeatable, NULL));
	ut_assert_nextlinen("%08lx: 00", io_addr + 0x10);
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iod_repeat);

/* Test 'iod' with a bad command line */
static int cmd_test_iod_usage(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));

	/* an address is required */
	ut_asserteq(1, run_commandf("iod"));
	ut_assert_nextline("iod - IO space display");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("iod [.b, .w, .l]");

	/* an unsupported size is rejected without any output */
	ut_asserteq(1, run_commandf("iod.x %lx 1", io_addr));
	ut_asserteq(1, run_commandf("iod.q %lx 1", io_addr));
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iod_usage);

/* Test 'iow' writing a byte to I/O space */
static int cmd_test_iow_base(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	outb(0, io_addr);

	/* the write is silent; read it back to see that it happened */
	ut_assertok(run_commandf("iow.b %lx 55", io_addr));
	ut_assert_console_end();
	ut_asserteq(0x55, inb(io_addr));

	return 0;
}
IO_TEST(cmd_test_iow_base);

/* Test that 'iow' truncates the value to the size of the write */
static int cmd_test_iow_size(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));

	/*
	 * The emulator stores whatever value reaches it and hands the whole
	 * thing back on any read, so inb() shows exactly what the command
	 * passed to the accessor. A byte write drops everything above the
	 * bottom byte
	 */
	ut_assertok(run_commandf("iow.b %lx 1234", io_addr));
	ut_asserteq(0x34, inb(io_addr));

	/* a word write keeps two bytes */
	ut_assertok(run_commandf("iow.w %lx 1234", io_addr));
	ut_asserteq(0x1234, inb(io_addr));

	/* .l is the default, so this one keeps the lot */
	ut_assertok(run_commandf("iow %lx 123456", io_addr));
	ut_asserteq(0x123456, inb(io_addr));
	ut_assert_console_end();

	return 0;
}
IO_TEST(cmd_test_iow_size);

/* Test 'iow' with a bad command line */
static int cmd_test_iow_usage(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	outb(0x99, io_addr);

	/* both an address and a value are required */
	ut_asserteq(1, run_commandf("iow %lx", io_addr));
	ut_assert_nextline("iow - IO space modify");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("iow [.b, .w, .l]");

	/* an unsupported size is rejected without any output */
	ut_asserteq(1, run_commandf("iow.x %lx 11", io_addr));
	ut_asserteq(1, run_commandf("iow.q %lx 11", io_addr));
	ut_assert_console_end();

	/* none of the failed commands writes anything */
	ut_asserteq(0x99, inb(io_addr));

	return 0;
}
IO_TEST(cmd_test_iow_usage);
