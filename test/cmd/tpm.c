// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the tpm command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/*
 * These tests do not use UTF_DM. do_tpm() reads the currently selected TPM, a
 * static, before it dispatches a sub-command, so 'tpm device' cannot be used
 * to recover from the pointer being left behind by a driver-model test. The
 * commands therefore run against the tree U-Boot itself has bound, which stays
 * where it is.
 */
#define TPM_TEST_FLAGS	UTF_CONSOLE

/* Memory used by these tests */
#define TPM_ADDR	(CONFIG_SYS_LOAD_ADDR + 0x1000)
#define TPM_SIZE	0x10

/* an index which the emulated TPM has a non-volatile space for */
#define TPM_INDEX	0x1007

/* and one which it does not */
#define TPM_BAD_INDEX	0x9999

/* a PCR measurement: 20 bytes as 40 hex digits */
#define TPM_DIGEST	"0102030405060708090a0b0c0d0e0f1011121314"

/* the last line of the help text, which ends the usage message */
#define TPM_USAGE_LAST	"    - Write to space <index> from values <values...>."

/*
 * Select the emulated TPMv1.x device. Sandbox has a TPMv2.x device first, so
 * the default choice is the wrong one, and the current TPM is a static which
 * the driver-model state restored before each test leaves pointing into the
 * previous test's tree.
 */
static int select_tpm(struct unit_test_state *uts)
{
	ut_assertok(run_command("tpm device 1", 0));
	ut_assert_console_end();

	return 0;
}

/* Test 'tpm device' and 'tpm info' */
static int cmd_test_tpm_base(struct unit_test_state *uts)
{
	ut_assertok(run_command("tpm device", 0));
	ut_assert_nextline("device 0: Sandbox TPM2.x");
	ut_assert_nextline("device 1: sandbox TPM");
	ut_assert_console_end();

	ut_assertok(select_tpm(uts));

	ut_assertok(run_command("tpm info", 0));
	ut_assert_nextline("sandbox TPM");
	ut_assert_console_end();

	/* bringing the device up says nothing when it works */
	ut_assertok(run_command("tpm init", 0));
	ut_assertok(run_command("tpm startup TPM_ST_CLEAR", 0));
	ut_assertok(run_command("tpm self_test_full", 0));
	ut_assertok(run_command("tpm continue_self_test", 0));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_base, TPM_TEST_FLAGS);

/* Test 'tpm autostart', which does the whole opening sequence */
static int cmd_test_tpm_autostart(struct unit_test_state *uts)
{
	ut_assertok(select_tpm(uts));

	ut_assertok(run_command("tpm autostart", 0));
	ut_assert_console_end();

	/* the sub-commands are those of the selected device, whatever the name */
	ut_assertok(run_command("tpm2 info", 0));
	ut_assert_nextline("sandbox TPM");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_autostart, TPM_TEST_FLAGS);

/* Test 'tpm extend' adding a measurement to a PCR */
static int cmd_test_tpm_extend(struct unit_test_state *uts)
{
	ut_assertok(select_tpm(uts));
	ut_assertok(run_command("tpm autostart", 0));

	/*
	 * The emulation does not compute the hash, so the register it reports
	 * stays at zero; what this checks is that a 20-byte digest is accepted
	 * and 20 bytes come back
	 */
	ut_assertok(run_command("tpm extend 0 " TPM_DIGEST, 0));
	ut_assert_nextline("PCR value after execution of the command:");
	ut_assert_nextline(" 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00");
	ut_assert_nextline(" 00 00 00 00");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_extend, TPM_TEST_FLAGS);

/* Test the byte-string form of the non-volatile storage sub-commands */
static int cmd_test_tpm_nvram(struct unit_test_state *uts)
{
	u8 *buf;

	ut_assertok(select_tpm(uts));
	ut_assertok(run_command("tpm autostart", 0));

	buf = map_sysmem(TPM_ADDR, TPM_SIZE);
	memset(buf, '\0', TPM_SIZE);

	ut_assertok(run_commandf("tpm nv_define_space %#x 0 4", TPM_INDEX));
	ut_assert_nextline("tpm: define_space index=%#x, len=0x4, seq=0x2",
			   TPM_INDEX);
	ut_assert_console_end();

	ut_assertok(run_commandf("tpm nv_write_value %#x deadbeef", TPM_INDEX));
	ut_assert_nextline("tpm: nvwrite index=%#x, len=0x4", TPM_INDEX);
	ut_assert_console_end();

	ut_assertok(run_commandf("tpm nv_read_value %#x %#lx 4", TPM_INDEX,
				 (ulong)TPM_ADDR));
	ut_assert_nextline("tpm: nvread index=%#x, len=0x4, seq=0x2", TPM_INDEX);
	ut_assert_nextline("area content:");
	ut_assert_nextline(" de ad be ef");
	ut_assert_console_end();

	/* the read leaves the data in memory as well as showing it */
	ut_asserteq(0xde, buf[0]);
	ut_asserteq(0xad, buf[1]);
	ut_asserteq(0xbe, buf[2]);
	ut_asserteq(0xef, buf[3]);
	ut_asserteq(0, buf[4]);
	unmap_sysmem(buf);

	return 0;
}
CMD_TEST(cmd_test_tpm_nvram, TPM_TEST_FLAGS);

/* Test the type-string form, which reads into environment variables */
static int cmd_test_tpm_vars(struct unit_test_state *uts)
{
	ut_assertok(select_tpm(uts));
	ut_assertok(run_command("tpm autostart", 0));

	/* 'd' is a four-byte value, so the space is four bytes long */
	ut_assertok(run_commandf("tpm nv_define d %#x 0", TPM_INDEX));
	ut_assert_nextline("tpm: define_space index=%#x, len=0x4, seq=0x2",
			   TPM_INDEX);
	ut_assert_console_end();

	ut_assertok(run_commandf("tpm nv_write d %#x 0x12345678", TPM_INDEX));
	ut_assert_nextline("tpm: nvwrite index=%#x, len=0x4", TPM_INDEX);
	ut_assert_console_end();

	ut_assertok(run_commandf("tpm nv_read d %#x tpmvar", TPM_INDEX));
	ut_assert_nextline("tpm: nvread index=%#x, len=0x4, seq=0x2", TPM_INDEX);
	ut_assert_console_end();

	/* the value is written as a decimal number */
	ut_assertok(run_command("printenv tpmvar", 0));
	ut_assert_nextline("tpmvar=305419896");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_vars, TPM_TEST_FLAGS);

/* Test 'tpm raw_transfer' sending a command byte by byte */
static int cmd_test_tpm_raw(struct unit_test_state *uts)
{
	ut_assertok(select_tpm(uts));

	/* a full self test, which has nothing to report */
	ut_assertok(run_command("tpm raw_transfer 00c10000000a00000050", 0));
	ut_assert_nextline("tpm response:");
	ut_assert_nextline(" 00 00 00 00 00 00 00 00 00 00 00 00");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_raw, TPM_TEST_FLAGS);

/* Test the error paths */
static int cmd_test_tpm_missing(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("tpm device 5", 0));
	ut_assert_nextline("Couldn't set TPM 5 (rc = 1)");
	ut_assert_console_end();

	ut_assertok(select_tpm(uts));

	ut_asserteq(1, run_command("tpm startup FOO", 0));
	ut_assert_nextline("Couldn't recognize mode string: FOO");
	ut_assert_console_end();

	/* the emulation has no space at this index */
	ut_asserteq(1, run_commandf("tpm nv_read_value %#x %#lx 4",
				    TPM_BAD_INDEX, (ulong)TPM_ADDR));
	ut_assert_nextline("Invalid nv index %#x", TPM_BAD_INDEX);
	ut_assert_nextline("Error: -22");
	ut_assert_console_end();

	/* 'b' is a byte, so this asks for one value and gives two */
	ut_asserteq(1, run_commandf("tpm nv_write b %#x 1 2", TPM_INDEX));
	ut_assert_skip_to_line("  nv_write types_string index values...");
	ut_assert_nextline(TPM_USAGE_LAST);
	ut_assert_nextline_empty();
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_tpm_missing, TPM_TEST_FLAGS);
