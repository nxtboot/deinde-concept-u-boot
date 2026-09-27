// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the ioapic command, as run on QEMU
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <test/cmd.h>
#include <test/ut.h>

#define IOAPIC_HEADER	"I/O APIC at fec00000: ID 0, version 20, 24 entries"
#define IOAPIC_COLUMNS	"Pin  Vector  Dest  Delivery  Polarity  Trigger  Mask"

static int cmd_test_ioapic(struct unit_test_state *uts)
{
	/* U-Boot leaves every pin masked, so none is shown */
	ut_assertok(run_command("ioapic", 0));
	ut_assert_nextline(IOAPIC_HEADER);
	ut_assert_nextline(IOAPIC_COLUMNS);
	ut_assert_console_end();

	/* the same I/O APIC, given by address, showing masked pins too */
	ut_assertok(run_command("ioapic -a fec00000", 0));
	ut_assert_nextline(IOAPIC_HEADER);
	ut_assert_nextline(IOAPIC_COLUMNS);
	ut_assert_nextline("  0  00      00    fixed         high      edge     masked");
	ut_assert_skip_to_line(" 23  00      00    fixed         high      edge     masked");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_ioapic, UTF_CONSOLE);
