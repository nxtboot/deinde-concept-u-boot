// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the nand command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <env.h>
#include <malloc.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>
#include <linux/ctype.h>

/* Geometry of nand0, the smaller of the two emulated sandbox chips */
#define NAND0_SIZE	0x400000
#define NAND0_ERASE	0x2000
#define NAND0_PAGE	0x200
#define NAND0_OOB	16

/* Block of nand1 which cmd_test_nand_bad() marks bad */
#define NAND1_BAD	0x180000

/* Partition table used by cmd_test_nand_part() */
#define TEST_PARTS	"nand0:1m(boot),2m(kernel),-(rootfs)"

/**
 * setup_nand() - Make nand0 current and drop any partition table
 *
 * The current device is a global which every sub-command reads, and the
 * mtdparts tests leave a table in the environment, so put both back to a
 * known state before each test.
 *
 * @uts: Test state
 * Return: 0 if OK, other value on error
 */
static int setup_nand(struct unit_test_state *uts)
{
	ut_assertok(env_set("mtdids", NULL));
	ut_assertok(env_set("mtdparts", NULL));
	ut_assertok(run_command("nand device 0", 0));
	console_record_reset();

	return 0;
}

/**
 * check_dump() - Check the hex dump of a run of identical bytes
 *
 * Each line holds the offset, the bytes one space apart, two spaces and
 * then the same bytes as characters.
 *
 * @uts: Test state
 * @off: Offset the dump starts at, as it appears at the start of each line
 * @len: Number of bytes dumped, which must be a multiple of @perline
 * @perline: Number of bytes on each line
 * @val: Value every one of those bytes is expected to have
 * Return: 0 if OK, other value on error
 */
static int check_dump(struct unit_test_state *uts, uint off, uint len,
		      uint perline, u8 val)
{
	char ascii = isprint(val) && val < 0x80 ? val : '.';
	char line[128];
	uint i, j;
	int pos;

	for (i = 0; i < len; i += perline) {
		pos = snprintf(line, sizeof(line), "%08x:", off + i);
		for (j = 0; j < perline; j++)
			pos += snprintf(line + pos, sizeof(line) - pos, " %02x",
					val);
		pos += snprintf(line + pos, sizeof(line) - pos, "  ");
		for (j = 0; j < perline; j++)
			pos += snprintf(line + pos, sizeof(line) - pos, "%c",
					ascii);
		ut_assert_nextline("%s", line);
	}

	return 0;
}

/**
 * erase_block() - Erase one block of nand0 and swallow the progress report
 *
 * @uts: Test state
 * @off: Offset of the block
 * Return: 0 if OK, other value on error
 */
static int erase_block(struct unit_test_state *uts, uint off)
{
	char cmd[40];

	snprintf(cmd, sizeof(cmd), "nand erase %x %x", off, NAND0_ERASE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND erase: device 0 offset 0x%x, size 0x%x", off,
			   NAND0_ERASE);
	ut_assert_nextline("\rErasing at 0x%x -- 100%% complete.", off);
	ut_assert_nextline("OK");
	ut_assert_console_end();

	return 0;
}

/* Test the report of the chips which are present */
static int cmd_test_nand_base(struct unit_test_state *uts)
{
	ut_assertok(setup_nand(uts));

	/*
	 * The bad-block table is scanned the first time a block of a chip is
	 * queried and that sets NAND_BBT_SCANNED in the options, so query
	 * both chips here to report the same whatever has run before
	 */
	ut_assertok(run_command("nand bad", 0));
	ut_assertok(run_command("nand device 1", 0));
	ut_assertok(run_command("nand bad", 0));
	ut_assertok(run_command("nand device 0", 0));
	console_record_reset();

	ut_assertok(run_command("nand info", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 0: nand0, sector size 8 KiB");
	ut_assert_nextline("  Page size          512 b");
	ut_assert_nextline("  OOB size            16 b");
	ut_assert_nextline("  Erase size        8192 b");
	ut_assert_nextline("  ecc strength         1 bits");
	ut_assert_nextline("  ecc step size      256 b");
	ut_assert_nextline("  subpagesize        256 b");
	ut_assert_nextline("  options       0x40000100");
	ut_assert_nextline("  bbt options   0x00000000");
	ut_assert_nextline("Device 1: nand1, sector size 512 KiB");
	ut_assert_nextline("  Page size         4096 b");
	ut_assert_nextline("  OOB size           224 b");
	ut_assert_nextline("  Erase size      524288 b");
	ut_assert_nextline("  ecc strength         4 bits");
	ut_assert_nextline("  ecc step size      512 b");
	ut_assert_nextline("  subpagesize       1024 b");
	ut_assert_nextline("  options       0x40005000");
	ut_assert_nextline("  bbt options   0x00000000");
	ut_assert_console_end();

	/*
	 * The geometry variables describe the last chip printed rather than
	 * the current device
	 */
	ut_asserteq_str("1000", env_get("nand_writesize"));
	ut_asserteq_str("e0", env_get("nand_oobsize"));
	ut_asserteq_str("80000", env_get("nand_erasesize"));

	return 0;
}
CMD_TEST(cmd_test_nand_base, UTF_CONSOLE);

/* Test choosing the current device */
static int cmd_test_nand_device(struct unit_test_state *uts)
{
	ut_assertok(setup_nand(uts));

	ut_assertok(run_command("nand device 1", 0));
	ut_assert_nextline("Device 1: nand1... is now current device");
	ut_assert_console_end();

	/* Choosing it again is quiet, since nothing changes */
	ut_assertok(run_command("nand device 1", 0));
	ut_assert_console_end();

	/* With no argument the sub-command reports the chip it has chosen */
	ut_assertok(run_command("nand device", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 1: nand1, sector size 512 KiB");
	ut_assert_skip_to_line("  bbt options   0x00000000");
	ut_assert_console_end();
	ut_asserteq_str("1000", env_get("nand_writesize"));

	/* A device which is not there is refused and changes nothing */
	ut_asserteq(1, run_command("nand device 5", 0));
	ut_assert_nextline("no devices available");
	ut_assert_console_end();

	ut_assertok(run_command("nand device 0", 0));
	ut_assert_nextline("Device 0: nand0... is now current device");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nand_device, UTF_CONSOLE);

/* Test erasing a block, writing a page and reading it back */
static int cmd_test_nand_io(struct unit_test_state *uts)
{
	u8 *wbuf, *rbuf;
	char cmd[80];

	ut_assertok(setup_nand(uts));

	wbuf = malloc(NAND0_PAGE);
	ut_assertnonnull(wbuf);
	rbuf = malloc(NAND0_PAGE);
	ut_assertnonnull(rbuf);
	memset(wbuf, 0x5a, NAND0_PAGE);
	memset(rbuf, 0, NAND0_PAGE);

	ut_assertok(erase_block(uts, 0));

	snprintf(cmd, sizeof(cmd), "nand write %lx 0 %x",
		 (ulong)map_to_sysmem(wbuf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND write: device 0 offset 0x0, size 0x%x",
			   NAND0_PAGE);
	ut_assert_nextline(" %d bytes written: OK", NAND0_PAGE);
	ut_assert_console_end();

	/*
	 * The emulated chip flips as many bits in each ECC step as the
	 * threshold allows, so every read of it is one the ECC has had to
	 * correct. Such a read succeeds and says nothing about it
	 */
	snprintf(cmd, sizeof(cmd), "nand read %lx 0 %x",
		 (ulong)map_to_sysmem(rbuf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND read: device 0 offset 0x0, size 0x%x",
			   NAND0_PAGE);
	ut_assert_nextline(" %d bytes read: OK", NAND0_PAGE);
	ut_assert_console_end();

	ut_asserteq_mem(wbuf, rbuf, NAND0_PAGE);

	/* Without a size, a read covers the whole chip */
	free(rbuf);
	rbuf = malloc(NAND0_SIZE);
	ut_assertnonnull(rbuf);
	snprintf(cmd, sizeof(cmd), "nand read %lx 0",
		 (ulong)map_to_sysmem(rbuf));
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND read: device 0 whole chip");
	ut_assert_nextline(" %d bytes read: OK", NAND0_SIZE);
	ut_assert_console_end();

	ut_asserteq_mem(wbuf, rbuf, NAND0_PAGE);

	free(rbuf);
	free(wbuf);

	return 0;
}
CMD_TEST(cmd_test_nand_io, UTF_CONSOLE);

/* Test dumping a page without reading it into memory */
static int cmd_test_nand_dump(struct unit_test_state *uts)
{
	u8 *buf;
	char cmd[80];

	ut_assertok(setup_nand(uts));

	buf = malloc(NAND0_PAGE);
	ut_assertnonnull(buf);
	memset(buf, 0x5a, NAND0_PAGE);

	ut_assertok(erase_block(uts, 0));
	snprintf(cmd, sizeof(cmd), "nand write %lx 0 %x",
		 (ulong)map_to_sysmem(buf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	console_record_reset();

	/*
	 * A dump through the ECC shows what was written, and succeeds, both
	 * of which need the corrected read to be taken as a success
	 */
	ut_assertok(run_command("nand dump.ecc 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Page at offset 00000000 dump:");
	ut_assertok(check_dump(uts, 0, NAND0_PAGE, 16, 0x5a));
	ut_assert_nextline_empty();
	ut_assert_nextline("OOB:");
	ut_assertok(check_dump(uts, 0, NAND0_OOB, 8, 0xff));
	ut_assert_console_end();

	/* The spare area on its own leaves the data out */
	ut_assertok(run_command("nand dump.ecc.oob 0", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Page at offset 00000000 dump:");
	ut_assert_nextline_empty();
	ut_assert_nextline("OOB:");
	ut_assertok(check_dump(uts, 0, NAND0_OOB, 8, 0xff));
	ut_assert_console_end();

	/* A page past the end of the chip cannot be read */
	ut_asserteq(1, run_command("nand dump.ecc 400000", 0));
	ut_assert_nextline("Error reading page at offset %08x, -%d uncorrectable, dumping raw data",
			   NAND0_SIZE, EINVAL);
	console_record_reset();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_nand_dump, UTF_CONSOLE);

/* Test that an offset may be given as the name of an mtdparts partition */
static int cmd_test_nand_part(struct unit_test_state *uts)
{
	u8 *buf;
	char cmd[80];

	ut_assertok(setup_nand(uts));

	buf = malloc(NAND0_PAGE);
	ut_assertnonnull(buf);
	memset(buf, 0xa5, NAND0_PAGE);

	ut_assertok(env_set("mtdids", "nand0=nand0"));
	ut_assertok(env_set("mtdparts", TEST_PARTS));

	/* The kernel partition starts at 1MB, which is where the write lands */
	ut_assertok(erase_block(uts, 0x100000));

	snprintf(cmd, sizeof(cmd), "nand write %lx kernel %x",
		 (ulong)map_to_sysmem(buf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND write: device 0 offset 0x100000, size 0x%x",
			   NAND0_PAGE);
	ut_assert_nextline(" %d bytes written: OK", NAND0_PAGE);
	ut_assert_console_end();

	ut_assertok(run_command("nand dump.ecc 100000", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Page at offset 00100000 dump:");
	ut_assertok(check_dump(uts, 0x100000, NAND0_PAGE, 16, 0xa5));
	console_record_reset();

	/* A partition which does not exist is refused */
	snprintf(cmd, sizeof(cmd), "nand write %lx nosuchpart %x",
		 (ulong)map_to_sysmem(buf), NAND0_PAGE);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextlinen("NAND write: ");
	ut_assert_console_end();

	free(buf);
	ut_assertok(setup_nand(uts));

	return 0;
}
CMD_TEST(cmd_test_nand_part, UTF_CONSOLE);

/* Test that an operation must fit within the chip */
static int cmd_test_nand_bounds(struct unit_test_state *uts)
{
	char cmd[80];
	ulong addr;
	u8 *buf;

	ut_assertok(setup_nand(uts));

	buf = malloc(NAND0_PAGE);
	ut_assertnonnull(buf);
	addr = map_to_sysmem(buf);

	/* An offset at or past the end of the chip is refused */
	snprintf(cmd, sizeof(cmd), "nand read %lx %x %x", addr, NAND0_SIZE,
		 NAND0_PAGE);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND read: Offset exceeds device limit");
	ut_assert_console_end();

	/* So is a size which reaches past it */
	snprintf(cmd, sizeof(cmd), "nand read %lx %x %x", addr,
		 NAND0_SIZE - NAND0_PAGE, NAND0_PAGE * 2);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND read: Size exceeds partition or device limit");
	ut_assert_console_end();

	/* A size which is not a number is reported as such */
	snprintf(cmd, sizeof(cmd), "nand read %lx 0 fish", addr);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("NAND read: 'fish' is not a number");
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_nand_bounds, UTF_CONSOLE);

/* Test marking a block bad and listing the bad blocks */
static int cmd_test_nand_bad(struct unit_test_state *uts)
{
	char cmd[40];

	ut_assertok(setup_nand(uts));

	/*
	 * Use nand1, since a marker cannot be undone and the other tests all
	 * work on nand0
	 */
	ut_assertok(run_command("nand device 1", 0));
	console_record_reset();

	snprintf(cmd, sizeof(cmd), "nand markbad %x", NAND1_BAD);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("block 0x%08x successfully marked as bad",
			   NAND1_BAD);
	ut_assert_console_end();

	/*
	 * The listing may hold blocks another test marked, as well as those
	 * the bad-block table reserves for itself, so look for ours rather
	 * than reading the whole list
	 */
	ut_assertok(run_command("nand bad", 0));
	ut_assert_nextline_empty();
	ut_assert_nextline("Device 1 bad blocks:");
	ut_assert_skip_to_line("  0x%08x", NAND1_BAD);
	console_record_reset();

	/* An offset with no block number is a usage error */
	ut_asserteq(1, run_command("nand markbad", 0));
	ut_assert_skip_to_line("nand markbad off [...] - mark bad block(s) at offset (UNSAFE)");
	console_record_reset();

	return 0;
}
CMD_TEST(cmd_test_nand_bad, UTF_CONSOLE);

/* Test the usage message and the arguments which produce it */
static int cmd_test_nand_usage(struct unit_test_state *uts)
{
	ut_assertok(setup_nand(uts));

	/* The command needs a sub-command */
	ut_asserteq(1, run_command("nand", 0));
	ut_assert_nextline("nand - NAND sub-system");
	console_record_reset();

	/* An unknown sub-command produces the same message */
	ut_asserteq(1, run_command("nand nosuchthing", 0));
	ut_assert_nextline("nand - NAND sub-system");
	console_record_reset();

	/* read and write need a memory address and an offset */
	ut_asserteq(1, run_command("nand read 1000", 0));
	ut_assert_nextline("nand - NAND sub-system");
	console_record_reset();

	/* erase refuses to take the whole chip from a missing argument */
	ut_asserteq(1, run_command("nand erase 0", 0));
	ut_assert_nextline("nand - NAND sub-system");
	console_record_reset();

	/* biterr reaches a single byte, so it takes a bit of one */
	ut_asserteq(1, run_command("nand biterr 0 9", 0));
	ut_assert_nextline("bit position 0 to 7 is allowed");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_nand_usage, UTF_CONSOLE);
