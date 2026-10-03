// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the mtd command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <env.h>
#include <malloc.h>
#include <mapmem.h>
#include <mtd.h>
#include <test/cmd.h>
#include <test/ut.h>
#include <linux/mtd/mtd.h>

/* Geometry of nand0, the smaller of the two emulated sandbox chips */
#define NAND0_SIZE	0x400000
#define NAND0_ERASE	0x2000
#define NAND0_PAGE	0x200

/* Geometry of nand1, which only the bad-block test touches */
#define NAND1_SIZE	0x100000000ULL
#define NAND1_ERASE	0x80000
#define NAND1_PAGE	0x1000

/* Partition table used by cmd_test_mtd_part() */
#define TEST_PARTS	"nand0:1m(boot),2m(kernel),-(rootfs)"

/**
 * setup_mtd() - Drop any MTD partitions an earlier test left behind
 *
 * The mtd command lists and accepts MTD partitions, and the mtdparts tests
 * leave a table in the environment, so clear it here.
 * mtd_probe_devices() notices that the variables have gone and deletes the
 * partitions it made from them.
 *
 * @uts: Test state
 * Return: 0 if OK, other value on error
 */
static int setup_mtd(struct unit_test_state *uts)
{
	ut_assertok(env_set("mtdids", NULL));
	ut_assertok(env_set("mtdparts", NULL));
	ut_assertok(mtd_probe_devices());
	console_record_reset();

	return 0;
}

/**
 * check_dump() - Check the hex dump of a run of identical bytes
 *
 * Each line of the dump holds sixteen bytes, as two groups of eight with an
 * extra space between them, and ends in the space which follows the last
 * byte.
 *
 * @uts: Test state
 * @off: Offset the dump starts at, as it appears at the start of each line
 * @len: Number of bytes dumped, which must be a multiple of 16
 * @val: Value every one of those bytes is expected to have
 * Return: 0 if OK, other value on error
 */
static int check_dump(struct unit_test_state *uts, uint off, uint len, u8 val)
{
	char line[128];
	uint i, j;
	int pos;

	for (i = 0; i < len; i += 16) {
		pos = snprintf(line, sizeof(line), "0x%08x:\t", off + i);
		for (j = 0; j < 16; j++)
			pos += snprintf(line + pos, sizeof(line) - pos,
					"%02x %s", val, j == 7 ? " " : "");
		ut_assert_nextline("%s", line);
	}

	return 0;
}

/* Test listing the MTD devices */
static int cmd_test_mtd_base(struct unit_test_state *uts)
{
	ut_assertok(setup_mtd(uts));

	ut_assertok(run_command("mtd list", 0));
	ut_assert_nextline("List of MTD devices:");

	ut_assert_nextline("* nand0");
	ut_assert_nextline("  - device: nand-controller");
	ut_assert_nextline("  - parent: root_driver");
	ut_assert_nextline("  - driver: sand-nand");
	ut_assert_nextline("  - path: /nand-controller");
	ut_assert_nextline("  - type: NAND flash");
	ut_assert_nextline("  - block size: 0x%x bytes", NAND0_ERASE);
	ut_assert_nextline("  - min I/O: 0x%x bytes", NAND0_PAGE);
	ut_assert_nextline("  - OOB size: 16 bytes");
	ut_assert_nextline("  - OOB available: 8 bytes");
	ut_assert_nextline("  - ECC strength: 1 bits");
	ut_assert_nextline("  - ECC step size: 256 bytes");
	ut_assert_nextline("  - bitflip threshold: 1 bits");
	ut_assert_nextline("  - 0x000000000000-0x%012x : \"nand0\"", NAND0_SIZE);

	ut_assert_nextline("* nand1");
	ut_assert_nextline("  - device: nand-controller");
	ut_assert_nextline("  - parent: root_driver");
	ut_assert_nextline("  - driver: sand-nand");
	ut_assert_nextline("  - path: /nand-controller");
	ut_assert_nextline("  - type: NAND flash");
	ut_assert_nextline("  - block size: 0x%x bytes", NAND1_ERASE);
	ut_assert_nextline("  - min I/O: 0x%x bytes", NAND1_PAGE);
	ut_assert_nextline("  - OOB size: 224 bytes");
	ut_assert_nextline("  - OOB available: 166 bytes");
	ut_assert_nextline("  - ECC strength: 4 bits");
	ut_assert_nextline("  - ECC step size: 512 bytes");
	ut_assert_nextline("  - bitflip threshold: 3 bits");
	ut_assert_nextline("  - 0x000000000000-0x%012llx : \"nand1\"", NAND1_SIZE);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtd_base, UTF_CONSOLE);

/* Test erasing a block, writing a page and reading it back */
static int cmd_test_mtd_io(struct unit_test_state *uts)
{
	u8 *wbuf, *rbuf;
	char cmd[80];

	ut_assertok(setup_mtd(uts));

	wbuf = malloc(NAND0_PAGE);
	ut_assertnonnull(wbuf);
	rbuf = malloc(NAND0_PAGE);
	ut_assertnonnull(rbuf);
	memset(wbuf, 0x5a, NAND0_PAGE);
	memset(rbuf, 0, NAND0_PAGE);

	ut_assertok(run_command("mtd erase nand0 0 2000", 0));
	ut_assert_nextline("Erasing 0x00000000 ... 0x00001fff (1 eraseblock(s))");
	ut_assert_console_end();

	snprintf(cmd, sizeof(cmd), "mtd write nand0 %lx 0 %x",
		 (ulong)map_to_sysmem(wbuf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("Writing 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_console_end();

	/*
	 * The emulated chip flips as many bits in each ECC step as the
	 * threshold allows, so every read of it is one the ECC has had to
	 * correct. Such a read succeeds and says nothing about it
	 */
	snprintf(cmd, sizeof(cmd), "mtd read nand0 %lx 0 %x",
		 (ulong)map_to_sysmem(rbuf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("Reading 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_console_end();

	ut_asserteq_mem(wbuf, rbuf, NAND0_PAGE);

	free(rbuf);
	free(wbuf);

	return 0;
}
CMD_TEST(cmd_test_mtd_io, UTF_CONSOLE);

/* Test dumping a page without reading it into memory */
static int cmd_test_mtd_dump(struct unit_test_state *uts)
{
	ut_assertok(setup_mtd(uts));

	ut_assertok(run_command("mtd erase nand0 0 2000", 0));
	console_record_reset();

	ut_assertok(run_command("mtd dump nand0 0 200", 0));
	ut_assert_nextline("Reading 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_nextline_empty();
	ut_assert_nextline("Dump 512 data bytes from 0x00000000:");
	ut_assertok(check_dump(uts, 0, NAND0_PAGE, 0xff));
	ut_assert_console_end();

	/* Without a size, dump covers a single page */
	ut_assertok(run_command("mtd dump nand0", 0));
	ut_assert_nextline("Reading 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_nextline_empty();
	ut_assert_nextline("Dump 512 data bytes from 0x00000000:");
	ut_assertok(check_dump(uts, 0, NAND0_PAGE, 0xff));
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtd_dump, UTF_CONSOLE);

/* Test that an offset in a partition counts from the start of that partition */
static int cmd_test_mtd_part(struct unit_test_state *uts)
{
	char cmd[80];
	u8 *buf;

	ut_assertok(setup_mtd(uts));

	buf = malloc(NAND0_PAGE);
	ut_assertnonnull(buf);
	memset(buf, 0xa5, NAND0_PAGE);

	ut_assertok(env_set("mtdids", "nand0=nand0"));
	ut_assertok(env_set("mtdparts", TEST_PARTS));
	ut_assertok(mtd_probe_devices());
	console_record_reset();

	/* The listing gains the three partitions, indented by a tab */
	ut_assertok(run_command("mtd list", 0));
	ut_assert_skip_to_line("\t  - 0x000000000000-0x000000100000 : \"boot\"");
	ut_assert_nextline("\t  - 0x000000100000-0x000000300000 : \"kernel\"");
	ut_assert_nextline("\t  - 0x000000300000-0x000000400000 : \"rootfs\"");
	console_record_reset();

	/* Write the first page of the kernel partition, which starts at 1MB */
	ut_assertok(run_command("mtd erase kernel 0 2000", 0));
	ut_assert_nextline("Erasing 0x00000000 ... 0x00001fff (1 eraseblock(s))");
	ut_assert_console_end();

	snprintf(cmd, sizeof(cmd), "mtd write kernel %lx 0 %x",
		 (ulong)map_to_sysmem(buf), NAND0_PAGE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("Writing 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_console_end();

	/* The same page appears at 1MB when the chip is addressed directly */
	ut_assertok(run_command("mtd dump nand0 100000 200", 0));
	ut_assert_nextline("Reading 512 byte(s) (1 page(s)) at offset 0x00100000");
	ut_assert_nextline_empty();
	ut_assert_nextline("Dump 512 data bytes from 0x00100000:");
	ut_assertok(check_dump(uts, 0x100000, NAND0_PAGE, 0xa5));
	ut_assert_console_end();

	free(buf);
	ut_assertok(setup_mtd(uts));

	return 0;
}
CMD_TEST(cmd_test_mtd_part, UTF_CONSOLE);

/* Test the alignment rules for an offset and a size */
static int cmd_test_mtd_align(struct unit_test_state *uts)
{
	ut_assertok(setup_mtd(uts));

	/* An offset off a page boundary is refused */
	ut_asserteq(1, run_command("mtd dump nand0 100", 0));
	ut_assert_nextline("Offset not aligned with a page (0x%x)", NAND0_PAGE);
	ut_assert_console_end();

	/* A size off a page boundary is rounded up instead */
	ut_assertok(run_command("mtd dump nand0 0 40", 0));
	ut_assert_nextline("Size not on a page boundary (0x%x), rounding to 0x%x",
			   NAND0_PAGE, NAND0_PAGE);
	ut_assert_nextline("Reading 512 byte(s) (1 page(s)) at offset 0x00000000");
	ut_assert_nextline_empty();
	ut_assert_nextline("Dump 512 data bytes from 0x00000000:");
	console_record_reset();

	/* Erase wants both numbers on a block boundary */
	ut_asserteq(1, run_command("mtd erase nand0 100 2000", 0));
	ut_assert_nextline("Offset not aligned with a block (0x%x)", NAND0_ERASE);
	ut_assert_console_end();

	ut_asserteq(1, run_command("mtd erase nand0 0 100", 0));
	ut_assert_nextline("Size not a multiple of a block (0x%x)", NAND0_ERASE);
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtd_align, UTF_CONSOLE);

/* Test that an operation must fit within the device */
static int cmd_test_mtd_bounds(struct unit_test_state *uts)
{
	const uint last = NAND0_SIZE - NAND0_ERASE;
	char cmd[80];
	ulong addr;
	u8 *buf;

	ut_assertok(setup_mtd(uts));

	buf = malloc(NAND0_ERASE);
	ut_assertnonnull(buf);
	addr = map_to_sysmem(buf);

	/* Without a size, a read covers what is left of the device */
	snprintf(cmd, sizeof(cmd), "mtd read nand0 %lx %x", addr, last);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("Reading 8192 byte(s) (16 page(s)) at offset 0x%08x",
			   last);
	ut_assert_console_end();

	/* A size which reaches past the end is refused */
	snprintf(cmd, sizeof(cmd), "mtd read nand0 %lx %x %x", addr, last,
		 NAND0_ERASE * 2);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("Op does not fit in nand0 (%x)", NAND0_SIZE);
	ut_assert_console_end();

	/* So is an offset which starts at or past the end */
	snprintf(cmd, sizeof(cmd), "mtd read nand0 %lx %x", addr, NAND0_SIZE);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("Op does not fit in nand0 (%x)", NAND0_SIZE);
	ut_assert_console_end();

	/* Erase checks the same two things */
	ut_asserteq(1, run_command("mtd erase nand0 3fe000 4000", 0));
	ut_assert_nextline("Op does not fit in nand0 (%x)", NAND0_SIZE);
	ut_assert_console_end();

	ut_asserteq(1, run_command("mtd erase nand0 400000", 0));
	ut_assert_nextline("Op does not fit in nand0 (%x)", NAND0_SIZE);
	ut_assert_console_end();

	/* and takes its default size from the offset too */
	ut_assertok(run_command("mtd erase nand0 3fe000", 0));
	ut_assert_nextline("Erasing 0x%08x ... 0x%08x (1 eraseblock(s))", last,
			   NAND0_SIZE - 1);
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_mtd_bounds, UTF_CONSOLE);

/* Test the usage message and a device which does not exist */
static int cmd_test_mtd_usage(struct unit_test_state *uts)
{
	ut_assertok(setup_mtd(uts));

	/* Read and write need a memory address */
	ut_asserteq(1, run_command("mtd read nand0", 0));
	ut_assert_skip_to_line("mtd - generic operations on memory technology devices");
	console_record_reset();

	ut_asserteq(1, run_command("mtd write nand0", 0));
	ut_assert_skip_to_line("mtd - generic operations on memory technology devices");
	console_record_reset();

	/* An unknown sub-command produces the same message */
	ut_asserteq(1, run_command("mtd nosuchthing nand0", 0));
	ut_assert_skip_to_line("mtd - generic operations on memory technology devices");
	console_record_reset();

	/* A name which is not a device reports the error from the MTD stack */
	ut_asserteq(1, run_command("mtd dump nosuchdev", 0));
	ut_assert_nextline("MTD device nosuchdev not found, ret -19");
	ut_assert_console_end();

	ut_asserteq(1, run_command("mtd erase nosuchdev", 0));
	ut_assert_nextline("MTD device nosuchdev not found, ret -19");
	ut_assert_console_end();

	ut_asserteq(1, run_command("mtd bad nosuchdev", 0));
	ut_assert_nextline("MTD device nosuchdev not found, ret -19");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_mtd_usage, UTF_CONSOLE);

/*
 * Test bad blocks, using nand1 so that the marker this leaves behind cannot
 * disturb the tests above, which all work on nand0
 */
static int cmd_test_mtd_bad(struct unit_test_state *uts)
{
	const u64 last = NAND1_SIZE - NAND1_ERASE;
	struct mtd_info *mtd;
	char cmd[80];
	u8 *buf;

	ut_assertok(setup_mtd(uts));

	/* Nothing in sandbox marks a block bad, so do it here */
	mtd = get_mtd_device_nm("nand1");
	ut_assertnonnull(mtd);
	ut_assertok(mtd_block_markbad(mtd, last));
	put_mtd_device(mtd);
	console_record_reset();

	/*
	 * The listing may hold the blocks the bad-block table reserves for
	 * itself as well, so look for ours rather than reading the whole list
	 */
	ut_assertok(run_command("mtd bad nand1", 0));
	ut_assert_nextline("MTD device nand1 bad blocks list:");
	ut_assert_skip_to_line("\t0x%08llx", last);
	console_record_reset();

	/* Erase keeps to the range it is given, saying what it passed over */
	snprintf(cmd, sizeof(cmd), "mtd erase nand1 %llx %x", last,
		 NAND1_ERASE);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextline("Erasing 0x%08llx ... 0x%08llx (1 eraseblock(s))",
			   last, NAND1_SIZE - 1);
	ut_assert_nextline("Skipping bad block at 0x%08llx", last);
	ut_assert_console_end();

	/*
	 * A read skips the block instead, which here leaves it with no device
	 * to read from
	 */
	buf = malloc(NAND1_PAGE);
	ut_assertnonnull(buf);
	snprintf(cmd, sizeof(cmd), "mtd read nand1 %lx %llx %x",
		 (ulong)map_to_sysmem(buf), last, NAND1_PAGE);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("Reading 4096 byte(s) (1 page(s)) at offset 0x%08llx",
			   last);
	ut_assert_nextline("Ran out of good blocks on nand1");
	ut_assert_nextline("Read on nand1 failed with error -28");
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_mtd_bad, UTF_CONSOLE);
