// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the clone command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <blk.h>
#include <blkmap.h>
#include <command.h>
#include <dm.h>
#include <malloc.h>
#include <test/cmd.h>
#include <test/ut.h>
#include <linux/sizes.h>

/* Block size of a blkmap device */
#define BLKSZ		512

/*
 * Blocks in a device large enough for a whole copy. The command works
 * through a 1MB buffer, so anything smaller runs out on the first read
 */
#define FULL_BLOCKS	(SZ_1M / BLKSZ)

/* Blocks in a device too small to fill that buffer */
#define SHORT_BLOCKS	8

/*
 * Every test here carries UTF_DM, even though the devices it uses are created
 * by the test rather than read from the devicetree. Without it the test runs
 * against whatever tree the last test left behind, where of_live_active() no
 * longer matches the ofnode the root device was bound with, and probing a
 * device whose parent is that root reads the wrong devicetree
 */

/**
 * struct clone_dev - A memory-backed block device for the tests
 *
 * @dev: blkmap device
 * @mem: Memory holding the contents of the device
 * @seq: Device number, which is what clone takes as its <dev> argument
 * @size: Size of the device in bytes
 */
struct clone_dev {
	struct udevice *dev;
	void *mem;
	int seq;
	ulong size;
};

/**
 * make_dev() - Create a block device backed by a fresh piece of memory
 *
 * The memory starts out zeroed, so a test can tell copied blocks from
 * untouched ones without writing anything to the destination first.
 *
 * @uts: Test state
 * @cd: Device to fill in
 * @label: blkmap label, which must not be in use
 * @blocks: Size of the device in 512-byte blocks
 * Return: 0 if OK, other value on error
 */
static int make_dev(struct unit_test_state *uts, struct clone_dev *cd,
		    const char *label, ulong blocks)
{
	cd->size = blocks * BLKSZ;
	cd->mem = calloc(1, cd->size);
	ut_assertnonnull(cd->mem);

	ut_assertok(blkmap_create(label, &cd->dev));
	ut_assertok(blkmap_map_mem(cd->dev, 0, blocks, cd->mem));
	cd->seq = dev_seq(cd->dev);

	return 0;
}

/**
 * free_dev() - Drop a device made by make_dev()
 *
 * @uts: Test state
 * @cd: Device to drop
 * Return: 0 if OK, other value on error
 */
static int free_dev(struct unit_test_state *uts, struct clone_dev *cd)
{
	ut_assertok(blkmap_destroy(cd->dev));
	free(cd->mem);

	return 0;
}

/* Test copying a megabyte from one device to another */
static int cmd_test_clone_base(struct unit_test_state *uts)
{
	struct clone_dev src, dst;
	char cmd[50];
	u32 *data;
	uint i;

	ut_assertok(make_dev(uts, &src, "clone-src", FULL_BLOCKS));
	ut_assertok(make_dev(uts, &dst, "clone-dst", FULL_BLOCKS));

	/* give every word of the source a different value */
	data = src.mem;
	for (i = 0; i < src.size / sizeof(u32); i++)
		data[i] = 0x12345678 + i;

	sprintf(cmd, "clone blkmap %d blkmap %d 1M", src.seq, dst.seq);
	ut_assertok(run_command(cmd, 0));
	ut_assert_nextlinen("Copying 1048576 bytes from blkmap:");
	ut_assert_nextline("1048576 read");
	ut_assert_nextline("1048576 written");

	/*
	 * The last line gives the elapsed time, and the rate as well when that
	 * time is at least a millisecond. Both depend on how fast the machine
	 * is, so drop the line rather than assert on it
	 */
	console_record_reset();

	ut_asserteq_mem(src.mem, dst.mem, src.size);

	ut_assertok(free_dev(uts, &dst));
	ut_assertok(free_dev(uts, &src));

	return 0;
}
CMD_TEST(cmd_test_clone_base,
	 UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test the arguments the command refuses */
static int cmd_test_clone_bad(struct unit_test_state *uts)
{
	struct clone_dev src;
	char cmd[50];

	ut_assertok(make_dev(uts, &src, "clone-src", SHORT_BLOCKS));

	/* the size is required, so five arguments are not enough */
	sprintf(cmd, "clone blkmap %d blkmap %d", src.seq, src.seq);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("clone - simple storage cloning");

	/* the rest is the usage text, which this test has nothing to say about */
	console_record_reset();

	/* an interface which does not exist is reported before any copying */
	sprintf(cmd, "clone bogus 0 blkmap %d 0", src.seq);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("Unable to open source device");
	ut_assert_console_end();

	sprintf(cmd, "clone blkmap %d bogus 0 0", src.seq);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextline("Unable to open destination device");
	ut_assert_console_end();

	ut_assertok(free_dev(uts, &src));

	return 0;
}
CMD_TEST(cmd_test_clone_bad,
	 UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test a source device smaller than the 1MB buffer the command copies
 * through. The first read is short and the next one reads nothing at all,
 * which has to end the copy rather than be retried for ever
 */
static int cmd_test_clone_short(struct unit_test_state *uts)
{
	struct clone_dev src, dst;
	char cmd[50];

	ut_assertok(make_dev(uts, &src, "clone-src", SHORT_BLOCKS));
	ut_assertok(make_dev(uts, &dst, "clone-dst", SHORT_BLOCKS));

	/* ask for twice what the source holds */
	sprintf(cmd, "clone blkmap %d blkmap %d 8192", src.seq, dst.seq);
	ut_asserteq(1, run_command(cmd, 0));
	ut_assert_nextlinen("Copying 8192 bytes from blkmap:");
	ut_assert_nextline("Src read error @blk 8");
	ut_assert_nextline("4096 read");
	ut_assert_nextline("0 written");
	console_record_reset();

	/* nothing is written when the read fails, so the copy stops at once */
	ut_asserteq(0, *(u8 *)dst.mem);

	ut_assertok(free_dev(uts, &dst));
	ut_assertok(free_dev(uts, &src));

	return 0;
}
CMD_TEST(cmd_test_clone_short,
	 UTF_CONSOLE | UTF_DM | UTF_SCAN_PDATA | UTF_SCAN_FDT);
