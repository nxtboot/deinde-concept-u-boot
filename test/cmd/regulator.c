// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the regulator command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <test/cmd.h>
#include <test/ut.h>

#define REG_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* An LDO whose driver reports a voltage, a current and a mode */
#define REG_EMMC	"VDD_EMMC_1.8V"

/* An LDO with a one-point voltage constraint and no current method */
#define REG_LCD		"VDD_LCD_3.3V"

/* What the LCD supply says when a voltage is outside its constraints */
#define REG_LCD_LIMITS \
	"Value exceeds regulator constraint limits 3300000..3300000 uV"

/*
 * Select a regulator and throw away what that prints
 *
 * Every test does this, since the command remembers its device in a static
 * variable which the driver-model state of an earlier test may have left
 * pointing at a device this test knows nothing about.
 */
static int select_reg(struct unit_test_state *uts, const char *name)
{
	ut_assertok(run_commandf("regulator dev %s", name));
	console_record_reset();

	return 0;
}

/* Test 'regulator list' and selecting a supply */
static int cmd_test_regulator_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("regulator list", 0));
	ut_assert_nextline("| %-20s| %-32s| %s", "Device", "regulator-name",
			   "Parent");
	ut_assert_nextline("| %-20s| %-32s| %s", "buck1", "SUPPLY_1.2V",
			   "sandbox_pmic@40");
	ut_assert_skip_to_line("| %-20s| %-32s| %s", "ldo1", REG_EMMC,
			       "sandbox_pmic@40");

	/*
	 * the SCMI supplies come after everything else, but only
	 * sandbox_defconfig has an SCMI agent, so with any other config the
	 * list stops short of them
	 */
	if (IS_ENABLED(CONFIG_DM_REGULATOR_SCMI)) {
		ut_assert_skip_to_line("| %-20s| %-32s| %s", "reg@1",
				       "sandbox-voltd1", "protocol@17");
		ut_assert_console_end();
	} else {
		ut_assert_skip_to_line("| %-20s| %-32s| %s", "ldo3",
				       "SUPPLY_1.8_3.3V", "sandbox_pmic@40");
		console_record_reset();
	}

	/* the name to select with is the regulator-name, not the device name */
	ut_assertok(run_command("regulator dev " REG_EMMC, 0));
	ut_assert_nextline("dev: " REG_EMMC " @ ldo1");
	ut_assert_console_end();

	/* with no name it reports the supply which is current */
	ut_assertok(run_command("regulator dev", 0));
	ut_assert_nextline("dev: " REG_EMMC " @ ldo1");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_base, REG_TEST_FLAGS);

/* Test 'regulator info' showing the devicetree constraints */
static int cmd_test_regulator_info(struct unit_test_state *uts)
{
	ut_assertok(select_reg(uts, REG_EMMC));

	ut_assertok(run_command("regulator info", 0));
	ut_assert_nextline("Regulator info:");
	ut_assert_nextline("* regulator-name:  " REG_EMMC);
	ut_assert_nextline("* device name:     ldo1");
	ut_assert_nextline("* parent name:     sandbox_pmic@40");
	ut_assert_nextline("* parent uclass:   pmic");
	ut_assert_nextlinen("* constraints:");
	ut_assert_nextline("  - min uV:        1800000");
	ut_assert_nextline("  - max uV:        1800000");
	ut_assert_nextline("  - min uA:        100000");
	ut_assert_nextline("  - max uA:        100000");
	ut_assert_nextline("  - always on:     0 (false)");
	ut_assert_nextline("  - boot on:       1 (true)");

	/* the mode ids come from the driver, not from the devicetree */
	ut_assert_nextline("* op modes:        4");
	ut_assert_nextline("  - mode id:       0 (OFF)");
	ut_assert_nextline("  - mode id:       1 (ON)");
	ut_assert_nextline("  - mode id:       2 (SLEEP)");
	ut_assert_nextline("  - mode id:       3 (STANDBY)");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_info, REG_TEST_FLAGS);

/* Test 'regulator status' for one supply and for all of them */
static int cmd_test_regulator_status(struct unit_test_state *uts)
{
	ut_assertok(select_reg(uts, REG_EMMC));

	ut_assertok(run_command("regulator status", 0));
	ut_assert_nextline("Regulator " REG_EMMC " status:");
	ut_assert_nextline(" * enable:         1 (true)");
	ut_assert_nextline(" * value uV:       1800000");
	ut_assert_nextline(" * current uA:     100000");
	ut_assert_nextline(" * mode id:        1 (ON)");
	ut_assert_console_end();

	/* -a gives a line per supply, with the probe status at the end */
	ut_assertok(run_command("regulator status -a", 0));
	ut_assert_nextline("%-20s %-10s %10s %10s %-10s %s", "Name", "Enabled",
			   "uV", "mA", "Mode", "Status");
	ut_assert_nextline("%-20s %-10s %10d %10d %-10s %i", "SUPPLY_1.2V",
			   "enabled", 1200000, 200000, "ON", 0);

	/* a driver which cannot report a value shows a dash for it */
	ut_assert_nextline("%-20s %-10s %10d %10s %-10s %i", "SUPPLY_3.3V",
			   "disabled", 3000000, "-", "OFF", 0);
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_regulator_status, REG_TEST_FLAGS);

/* Test 'regulator value', including the constraint check and -f */
static int cmd_test_regulator_value(struct unit_test_state *uts)
{
	ut_assertok(select_reg(uts, REG_LCD));

	ut_assertok(run_command("regulator value", 0));
	ut_assert_nextline("3000000 uV");
	ut_assert_console_end();

	/* the constraints allow one value only */
	ut_assertok(run_command("regulator value 3300000", 0));
	ut_assert_console_end();

	ut_assertok(run_command("regulator value", 0));
	ut_assert_nextline("3300000 uV");
	ut_assert_console_end();

	ut_asserteq(1, run_command("regulator value 3400000", 0));
	ut_assert_nextline(REG_LCD_LIMITS);
	ut_assert_console_end();

	/* -f sets it anyway, so long as the driver can produce it */
	ut_assertok(run_command("regulator value 3400000 -f", 0));
	ut_assert_console_end();

	ut_assertok(run_command("regulator value", 0));
	ut_assert_nextline("3400000 uV");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_value, REG_TEST_FLAGS);

/* Test 'regulator enable', 'disable' and 'mode' */
static int cmd_test_regulator_mode(struct unit_test_state *uts)
{
	ut_assertok(select_reg(uts, REG_EMMC));

	ut_assertok(run_command("regulator mode", 0));
	ut_assert_nextline("mode id: 1");
	ut_assert_console_end();

	ut_assertok(run_command("regulator disable", 0));
	ut_assert_console_end();

	/* disabling drops the mode to OFF as well as clearing enable */
	ut_assertok(run_command("regulator status", 0));
	ut_assert_nextline("Regulator " REG_EMMC " status:");
	ut_assert_nextline(" * enable:         0 (false)");
	ut_assert_skip_to_line(" * mode id:        0 (OFF)");
	ut_assert_console_end();

	ut_assertok(run_command("regulator enable", 0));
	ut_assert_console_end();

	ut_assertok(run_command("regulator mode", 0));
	ut_assert_nextline("mode id: 1");
	ut_assert_console_end();

	/* a mode the driver knows about can be selected directly */
	ut_assertok(run_command("regulator mode 2", 0));
	ut_assert_console_end();

	ut_assertok(run_command("regulator mode", 0));
	ut_assert_nextline("mode id: 2");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_mode, REG_TEST_FLAGS);

/* Test the paths which report an error */
static int cmd_test_regulator_missing(struct unit_test_state *uts)
{
	/* the device name is not the name this command wants */
	ut_asserteq(1, run_command("regulator dev ldo1", 0));
	ut_assert_nextline("Can't get the regulator: ldo1!");
	ut_assert_nextline("Error: -19 (No such device)");
	ut_assert_console_end();

	ut_assertok(select_reg(uts, REG_LCD));

	/* this LDO has no way to report a current */
	ut_asserteq(1, run_command("regulator current", 0));
	ut_assert_nextline("Regulator: " REG_LCD " - can't get the Current!");
	ut_assert_nextline("Error: -38 (Function not implemented)");
	ut_assert_console_end();

	/* a current outside the constraints is refused, with no -f to force */
	ut_asserteq(1, run_command("regulator current 500", 0));
	ut_assert_nextline("Current exceeds regulator constraint limits");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_missing, REG_TEST_FLAGS);

/* Test 'regulator' with a bad command line */
static int cmd_test_regulator_usage(struct unit_test_state *uts)
{
	ut_assertok(select_reg(uts, REG_EMMC));

	/* a sub-command is required */
	ut_asserteq(1, run_command("regulator", 0));
	ut_assert_nextline("regulator - uclass operations");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_linen("regulator disable");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* an unknown sub-command is refused */
	ut_asserteq(1, run_command("regulator nosuchsubcmd", 0));
	ut_assert_nextline("regulator - uclass operations");
	ut_assert_skip_to_linen("regulator disable");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* status takes -a and nothing else */
	ut_asserteq(1, run_command("regulator status -x", 0));
	ut_assert_nextline("regulator - uclass operations");
	ut_assert_skip_to_linen("regulator disable");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	/* enable takes no arguments */
	ut_asserteq(1, run_command("regulator enable now", 0));
	ut_assert_nextline("regulator - uclass operations");
	ut_assert_skip_to_linen("regulator disable");
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_regulator_usage, REG_TEST_FLAGS);
