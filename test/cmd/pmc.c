// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the pmc command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <power/acpi_pmc.h>
#include <test/cmd.h>
#include <test/ut.h>

/* the controller sits behind the emulated PCI bus, so the tree is needed */
#define PMC_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* the last line of the help text, which ends the usage message */
#define PMC_USAGE_LAST	"pmc init - read state from the PMC"

/* the last line of the report, which the sandbox controller leaves clear */
#define PMC_GEN_PMCON \
	"gen_pmcon1: 00000000 gen_pmcon2: 00000000 gen_pmcon3: 00000000"

/* Check the usage message, which every bad command line produces */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("pmc - Power-management controller info");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line(PMC_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}

/* Test 'pmc info' showing the state of the controller */
static int cmd_test_pmc_base(struct unit_test_state *uts)
{
	int i;

	ut_assertok(run_command("pmc info", 0));

	ut_assert_nextline("Device: pci@1e,0");

	/*
	 * The register regions are shown as pointers, so where they land
	 * differs from run to run; only the start of the line is fixed.
	 */
	ut_assert_nextlinen("ACPI base 0, pmc_bar0 ");

	ut_assert_nextline("pm1_sts: 0000 pm1_en: 0002 pm1_cnt: 00000004");

	/*
	 * The sandbox I/O emulation returns the address which is read, so
	 * each bank reports its own register offset.
	 */
	for (i = 0; i < GPE0_REG_MAX; i++) {
		ut_assert_nextline("gpe0_sts[%d]: %08x gpe0_en[%d]: %08x", i,
				   GPE0_STS + i * 4, i, GPE0_EN + i * 4);
	}

	ut_assert_nextline("prsts: 00000000");
	ut_assert_nextline("tco_sts:   0064 0066");
	ut_assert_nextline(PMC_GEN_PMCON);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_pmc_base, PMC_TEST_FLAGS);

/* Test that 'pmc init' reads the state without showing it */
static int cmd_test_pmc_init(struct unit_test_state *uts)
{
	struct acpi_pmc_upriv *upriv;
	struct udevice *dev;

	ut_assertok(uclass_first_device_err(UCLASS_ACPI_PMC, &dev));
	upriv = dev_get_uclass_priv(dev);

	/* wipe two of the values so that a fresh read can be seen */
	upriv->gpe0_sts[1] = 0;
	upriv->tco1_sts = 0;

	ut_assertok(run_command("pmc init", 0));
	ut_assert_console_end();

	ut_asserteq(GPE0_STS + 4, upriv->gpe0_sts[1]);
	ut_asserteq(0x64, upriv->tco1_sts);

	return 0;
}
CMD_TEST(cmd_test_pmc_init, PMC_TEST_FLAGS);

/* Test 'pmc' with a bad command line */
static int cmd_test_pmc_usage(struct unit_test_state *uts)
{
	/* a sub-command is needed */
	ut_asserteq(1, run_command("pmc", 0));
	ut_assertok(check_usage(uts));

	/* and it must be one the command knows */
	ut_asserteq(1, run_command("pmc bogus", 0));
	ut_assertok(check_usage(uts));

	/* neither sub-command takes an argument */
	ut_asserteq(1, run_command("pmc info extra", 0));
	ut_assertok(check_usage(uts));

	return 0;
}
CMD_TEST(cmd_test_pmc_usage, PMC_TEST_FLAGS);
