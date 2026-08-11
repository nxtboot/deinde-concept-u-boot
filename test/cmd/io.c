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
	console_record_reset();

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
	console_record_reset();

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
	console_record_reset();

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

/* Test 'iod' with a bad command line */
static int cmd_test_iod_usage(struct unit_test_state *uts)
{
	ulong io_addr;

	ut_assertok(get_io_addr(uts, &io_addr));
	console_record_reset();

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
