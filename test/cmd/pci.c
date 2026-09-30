// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the pci command
 *
 * Copyright 2026 Simon Glass
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * Test listing all buses. Sandbox's buses are not numbered contiguously:
 * pci4 is bus 0x10, with a bridge to bus 0x11, the bridge on pci2 is bus
 * 0x20 and pci5's two bridges are 0x21 and 0x22. They are listed in order,
 * although the bridge on pci2 is bound before pci3 and pci4
 */
static int cmd_test_pci_list(struct unit_test_state *uts)
{
	ut_assertok(run_command("pci", 0));
	ut_assert_nextline("BusDevFun  VendorId   DeviceId   Device Class       Sub-Class");
	ut_assert_nextlinen("_____");
	ut_assert_skip_to_line("03.00.00   0x1234     0x5678     Simple comm. controller 0x00");
	ut_assert_nextline("05.01.00   0x1234     0x5678     Simple comm. controller 0x00");
	ut_assert_nextline("05.02.00   0x1234     0x5675     Bridge device           0x04");
	ut_assert_nextline("10.00.00   0x1234     0x5675     Bridge device           0x04");
	ut_assert_nextline("11.00.00   0x1234     0x5678     Simple comm. controller 0x00");
	ut_assert_nextline("20.00.00   0x1234     0x5678     Simple comm. controller 0x00");
	ut_assert_nextline("21.00.00   0x1234     0x5675     Bridge device           0x04");
	ut_assert_nextline("22.00.00   0x1234     0x5678     Simple comm. controller 0x00");
	ut_assert_console_end();

	/* the regions of each controller, but not of the bridges */
	ut_assertok(run_command("pci regions", 0));
	ut_assert_nextline("Buses 00-00");
	ut_assert_skip_to_line("Buses 03-03");
	ut_assert_skip_to_line("Buses 10-11");
	ut_assert_nextlinen("#   Bus start");
	ut_assert_nextlinen("0   0x00000000a0000000 0x00000000a0000000 0x0000000001000000  mem");
	ut_assert_nextlinen("1   0x00000000a1000000 0x00000000a1000000 0x0000000000010000  io");
	ut_assert_nextlinen("2   ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pci_list, UTF_CONSOLE);

/* Test 'pci intr' on the swap-case device, which has MSI-X but no INTx */
static int cmd_test_pci_intr(struct unit_test_state *uts)
{
	ut_assertok(run_command("pci intr 0.1f.0", 0));
	ut_assert_nextline("INTx: pin - line 255");
	ut_assert_nextline("MSI-X: disabled, 1 entries, table BAR1+0, PBA BAR1+800");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pci_intr, UTF_CONSOLE);
