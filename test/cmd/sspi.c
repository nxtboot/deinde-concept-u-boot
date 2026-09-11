// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the sspi command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <dm.h>
#include <test/cmd.h>
#include <test/ut.h>

#define SSPI_TEST_FLAGS	(UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT)

/* read the JEDEC ID of the emulated flash: command 9f and three idle bytes */
#define SSPI_READ_ID	"sspi 0:0 32 9f000000"

/* the ID of the m25p16 which sandbox has on both chip selects */
#define SSPI_M25P16_ID	"FF202015"

/* the two messages the SPI uclass gives for a chip select which is not there */
#define SSPI_BAD_CS	"sandbox_spi spi@0: Invalid cs 5 (err=-22)"
#define SSPI_BAD_CS2	"sandbox_spi spi@0: Invalid chip select 0:5 (err=-22)"

/* and the one it gives for a bus which is not there */
#define SSPI_BAD_BUS	"Invalid bus 1 (err=-19)"

/*
 * The names of the functions which log those, right-aligned in
 * CONFIG_LOGF_FUNC_PAD (20) characters as the log emits them
 */
#define SSPI_FUNC_FIND	"spi_find_chip_select"
#define SSPI_FUNC_GET	" _spi_get_bus_and_cs"

/* 64 hexadecimal digits are 32 bytes, which is all the buffer holds */
#define SSPI_32_BYTES \
	"9f00000000000000000000000000000000000000000000000000000000000000"

/* and 66 are 33 bytes, which is one too many */
#define SSPI_33_BYTES	SSPI_32_BYTES "00"

/* the reply to a 256-bit transfer of SSPI_32_BYTES */
#define SSPI_32_REPLY \
	"FF20201500000000000000000000000000000000000000000000000000000000"

/**
 * check_log() - Check the next console line, which the SPI uclass logs
 *
 * The log format shows the function name only when CONFIG_LOGF_FUNC is
 * enabled. That is true of sandbox but of none of the other sandbox
 * variants, so the expected line differs between them.
 *
 * @uts: Test state
 * @func: Name of the function which emits the message
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

/*
 * Do one transfer and throw the output away. The emulated flash announces
 * itself the first time it is probed, and each test rebuilds the driver-model
 * tree, so that message would otherwise turn up in the middle of a test.
 */
static int wake_flash(struct unit_test_state *uts)
{
	ut_assertok(run_command(SSPI_READ_ID, 0));
	ut_assertok(run_command("sspi 0:1 32 9f000000", 0));
	console_record_reset();

	return 0;
}

/* Test reading from the devices on the SPI bus */
static int cmd_test_sspi_base(struct unit_test_state *uts)
{
	ut_assertok(wake_flash(uts));

	/* the first byte comes back while the command byte is going out */
	ut_assertok(run_command(SSPI_READ_ID, 0));
	ut_assert_nextline(SSPI_M25P16_ID);
	ut_assert_console_end();

	/* sandbox has the same chip on chip select 1, in mode 3 */
	ut_assertok(run_command("sspi 0:1.3 32 9f000000", 0));
	ut_assert_nextline(SSPI_M25P16_ID);
	ut_assert_console_end();

	/* one number alone is a chip select on the default bus */
	ut_assertok(run_command("sspi 1 32 9f000000", 0));
	ut_assert_nextline(SSPI_M25P16_ID);
	ut_assert_console_end();

	/* command 05 reads the status register, which reports the chip idle */
	ut_assertok(run_command("sspi 0:0 16 0500", 0));
	ut_assert_nextline("FF00");
	ut_assert_console_end();

	/* the reply is (bit_len + 7) / 8 bytes, so 24 bits give three */
	ut_assertok(run_command("sspi 0:0 24 9f0000", 0));
	ut_assert_nextline("FF2020");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_sspi_base, SSPI_TEST_FLAGS);

/* Test that the values of the last command are used again */
static int cmd_test_sspi_repeat(struct unit_test_state *uts)
{
	ut_assertok(wake_flash(uts));

	ut_assertok(run_command(SSPI_READ_ID, 0));
	ut_assert_nextline(SSPI_M25P16_ID);
	ut_assert_console_end();

	/* without a data string the same bytes are sent again */
	ut_assertok(run_command("sspi 0:0 16", 0));
	ut_assert_nextline("FF20");
	ut_assert_console_end();

	/* without a length either, the whole transfer is repeated */
	ut_assertok(run_command("sspi 0:0", 0));
	ut_assert_nextline("FF20");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_sspi_repeat, SSPI_TEST_FLAGS);

/* Test the command line being wrong in each of the ways it can be */
static int cmd_test_sspi_bad(struct unit_test_state *uts)
{
	ut_assertok(wake_flash(uts));

	/* nothing at all gives the usage message */
	ut_asserteq(1, run_command("sspi", 0));
	ut_assert_nextline("sspi - SPI utility command");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_skip_to_line("<dout>    - Hexadecimal string that gets sent");
	ut_assert_console_end();

	/* there is no chip select 5 on the sandbox bus */
	ut_asserteq(1, run_command("sspi 5 8 00", 0));
	ut_assertok(check_log(uts, SSPI_FUNC_FIND, SSPI_BAD_CS));
	ut_assertok(check_log(uts, SSPI_FUNC_GET, SSPI_BAD_CS2));
	ut_assert_console_end();

	/* nor a bus 1 */
	ut_asserteq(1, run_command("sspi 1:0 8 00", 0));
	ut_assertok(check_log(uts, SSPI_FUNC_GET, SSPI_BAD_BUS));
	ut_assert_console_end();

	/* the buffer holds 32 bytes, so 256 bits is the most it can send */
	ut_asserteq(1, run_command("sspi 0:0 300 9f", 0));
	ut_assert_nextline("Invalid bitlen 300");
	ut_assert_console_end();

	/* and the data string may hold only hexadecimal digits */
	ut_asserteq(1, run_command("sspi 0:0 8 gg", 0));
	ut_assert_nextline("Hex conversion error on g");
	ut_assert_console_end();

	/*
	 * A string of 66 digits is 33 bytes, one more than the buffer holds.
	 * Without the length check it is converted anyway, over whatever
	 * follows dout[] in memory.
	 */
	ut_asserteq(1, run_command("sspi 0:0 8 " SSPI_33_BYTES, 0));
	ut_assert_nextline("Too many bytes (max 32)");
	ut_assert_console_end();

	/* 64 digits are exactly 32 bytes, which does fit */
	ut_assertok(run_command("sspi 0:0 256 " SSPI_32_BYTES, 0));
	ut_assert_nextline(SSPI_32_REPLY);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_sspi_bad, SSPI_TEST_FLAGS);
