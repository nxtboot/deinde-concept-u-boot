// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the pmic command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

#define PMIC_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* Name of the sandbox PMIC on the emulated I2C bus */
#define PMIC_NAME	"sandbox_pmic@40"

/* The chip has 16 one-byte registers, so 'pmic dump' is a single line */
#define PMIC_DUMP_LINE \
	"0x00: 08 00 00 2d 00 00 20 01 00 2d 00 00 00 00 00 00 "

/*
 * Select the sandbox PMIC and throw away what that prints
 *
 * Every test does this, since the command remembers its device in a static
 * variable which the driver-model state of an earlier test may have left
 * pointing at a device this test knows nothing about.
 */
static int select_pmic(struct unit_test_state *uts)
{
	ut_assertok(run_command("pmic dev " PMIC_NAME, 0));
	console_record_reset();

	return 0;
}

/* Test 'pmic list' and selecting a device */
static int cmd_test_pmic_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("pmic list", 0));
	ut_assert_nextline("| %-32s| %-20s| %s",
			   "Name", "Parent name", "Parent uclass @ seq");
	ut_assert_nextline("| %-32s| %-20s| i2c @ 0 | status: 0", PMIC_NAME,
			   "i2c@0");
	ut_assert_skip_to_line("| %-32s| %-20s| spmi @ 0 | status: 0",
			       "pm8916@0", "spmi@0");
	ut_assert_console_end();

	/* selecting a device reports its sequence number and name */
	ut_assertok(run_command("pmic dev " PMIC_NAME, 0));
	ut_assert_nextline("dev: 0 @ " PMIC_NAME);
	ut_assert_console_end();

	/* with no name it reports the device which is current */
	ut_assertok(run_command("pmic dev", 0));
	ut_assert_nextline("dev: 0 @ " PMIC_NAME);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_base, PMIC_TEST_FLAGS);

/* Test 'pmic dump' showing every register */
static int cmd_test_pmic_dump(struct unit_test_state *uts)
{
	ut_assertok(select_pmic(uts));

	ut_assertok(run_command("pmic dump", 0));
	ut_assert_nextline("Dump pmic: " PMIC_NAME " registers");
	ut_assert_nextline_empty();

	ut_assert_nextline(PMIC_DUMP_LINE);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_dump, PMIC_TEST_FLAGS);

/* Test 'pmic read' and 'pmic write' on a single register */
static int cmd_test_pmic_rw(struct unit_test_state *uts)
{
	ut_assertok(select_pmic(uts));

	ut_assertok(run_command("pmic read 0", 0));
	ut_assert_nextline("0x00: 0x08");
	ut_assert_console_end();

	/* a write says nothing and is visible to the next read */
	ut_assertok(run_command("pmic write 0 0x33", 0));
	ut_assert_console_end();

	ut_assertok(run_command("pmic read 0", 0));
	ut_assert_nextline("0x00: 0x33");
	ut_assert_console_end();

	/* the register number may be given in hex as well as decimal */
	ut_assertok(run_command("pmic read 0x3", 0));
	ut_assert_nextline("0x03: 0x2d");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_rw, PMIC_TEST_FLAGS);

/* Test a register number past the end of the chip */
static int cmd_test_pmic_range(struct unit_test_state *uts)
{
	ut_assertok(select_pmic(uts));

	/* the chip has 16 registers, so 15 is the last one which can be read */
	ut_assertok(run_command("pmic read 15", 0));
	ut_assert_nextline("0x0f: 0x00");
	ut_assert_console_end();

	ut_asserteq(1, run_command("pmic read 16", 0));
	ut_assert_nextline("PMIC max reg: 16");
	ut_assert_nextline("Error: -14 (Bad address)");
	ut_assert_console_end();

	ut_asserteq(1, run_command("pmic write 16 0", 0));
	ut_assert_nextline("PMIC max reg: 16");
	ut_assert_nextline("Error: -14 (Bad address)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_range, PMIC_TEST_FLAGS);

/* Test 'pmic dev' with a device which is not there */
static int cmd_test_pmic_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("pmic dev nosuchpmic", 0));
	ut_assert_nextline("Can't get PMIC: nosuchpmic!");
	ut_assert_nextline("Error: -19 (No such device)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_missing, PMIC_TEST_FLAGS);

/* Test 'pmic' with a bad command line */
static int cmd_test_pmic_usage(struct unit_test_state *uts)
{
	ut_assertok(select_pmic(uts));

	/* a sub-command is required */
	ut_asserteq(1, run_command("pmic", 0));
	ut_assert_nextline("pmic - PMIC sub-system");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("pmic write <reg> <byte>");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* an unknown sub-command is refused */
	ut_asserteq(1, run_command("pmic nosuchsubcmd", 0));
	ut_assert_nextline("pmic - PMIC sub-system");
	ut_assert_skip_to_linen("pmic write <reg> <byte>");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* read needs a register number */
	ut_asserteq(1, run_command("pmic read", 0));
	ut_assert_nextline("pmic - PMIC sub-system");
	ut_assert_skip_to_linen("pmic write <reg> <byte>");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* write needs a value as well */
	ut_asserteq(1, run_command("pmic write 0", 0));
	ut_assert_nextline("pmic - PMIC sub-system");
	ut_assert_skip_to_linen("pmic write <reg> <byte>");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmic_usage, PMIC_TEST_FLAGS);
