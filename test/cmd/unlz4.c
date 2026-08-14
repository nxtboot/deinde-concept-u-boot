// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the unlz4 command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <env.h>
#include <mapmem.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define SRC_ADDR	((ulong)CONFIG_SYS_LOAD_ADDR + 0x1000)
#define DST_ADDR	((ulong)CONFIG_SYS_LOAD_ADDR + 0x2000)
#define DST_SIZE	0x1000

/* The text which the frame below holds, from test/lib/compression.c */
static const char plain[] =
	"I am a highly compressable bit of text.\n"
	"I am a highly compressable bit of text.\n"
	"I am a highly compressable bit of text.\n"
	"There are many like me, but this one is mine.\n"
	"If I were any shorter, there wouldn't be much sense in\n"
	"compressing me in the first place. At least with lzo, anyway,\n"
	"which appears to behave poorly in the face of short text\n"
	"messages.\n";
#define PLAIN_SIZE	(sizeof(plain) - 1)

/*
 * An lz4 frame holding the text above, made with 'lz4 -z plain.txt' and taken
 * from test/lib/compression.c
 */
static const char lz4_frame[] =
	"\x04\x22\x4d\x18\x64\x70\xb9\x01\x01\x00\x00\xff\x19\x49\x20\x61"
	"\x6d\x20\x61\x20\x68\x69\x67\x68\x6c\x79\x20\x63\x6f\x6d\x70\x72"
	"\x65\x73\x73\x61\x62\x6c\x65\x20\x62\x69\x74\x20\x6f\x66\x20\x74"
	"\x65\x78\x74\x2e\x0a\x28\x00\x3d\xf1\x25\x54\x68\x65\x72\x65\x20"
	"\x61\x72\x65\x20\x6d\x61\x6e\x79\x20\x6c\x69\x6b\x65\x20\x6d\x65"
	"\x2c\x20\x62\x75\x74\x20\x74\x68\x69\x73\x20\x6f\x6e\x65\x20\x69"
	"\x73\x20\x6d\x69\x6e\x65\x2e\x0a\x49\x66\x20\x49\x20\x77\x32\x00"
	"\xd1\x6e\x79\x20\x73\x68\x6f\x72\x74\x65\x72\x2c\x20\x74\x45\x00"
	"\xf4\x0b\x77\x6f\x75\x6c\x64\x6e\x27\x74\x20\x62\x65\x20\x6d\x75"
	"\x63\x68\x20\x73\x65\x6e\x73\x65\x20\x69\x6e\x0a\xcf\x00\x50\x69"
	"\x6e\x67\x20\x6d\x12\x00\x00\x32\x00\xf0\x11\x20\x66\x69\x72\x73"
	"\x74\x20\x70\x6c\x61\x63\x65\x2e\x20\x41\x74\x20\x6c\x65\x61\x73"
	"\x74\x20\x77\x69\x74\x68\x20\x6c\x7a\x6f\x2c\x63\x00\xf5\x14\x77"
	"\x61\x79\x2c\x0a\x77\x68\x69\x63\x68\x20\x61\x70\x70\x65\x61\x72"
	"\x73\x20\x74\x6f\x20\x62\x65\x68\x61\x76\x65\x20\x70\x6f\x6f\x72"
	"\x6c\x79\x4e\x00\x30\x61\x63\x65\x27\x01\x01\x95\x00\x01\x2d\x01"
	"\xb0\x0a\x6d\x65\x73\x73\x61\x67\x65\x73\x2e\x0a\x00\x00\x00\x00"
	"\x9d\x12\x8c\x9d";
#define FRAME_SIZE	(sizeof(lz4_frame) - 1)

/**
 * setup_frame() - Put the compressed frame in memory and clear the output
 *
 * @dstp: Returns a pointer to the destination region, which is zeroed
 * Return: pointer to the source region, holding the frame
 */
static void *setup_frame(void **dstp)
{
	void *src, *dst;

	src = map_sysmem(SRC_ADDR, FRAME_SIZE);
	memcpy(src, lz4_frame, FRAME_SIZE);

	dst = map_sysmem(DST_ADDR, DST_SIZE);
	memset(dst, '\0', DST_SIZE);
	*dstp = dst;

	return src;
}

/* Test 'unlz4' decompressing a frame */
static int cmd_test_unlz4_base(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_frame(&dst);

	ut_assertok(run_commandf("unlz4 %lx %lx %x", SRC_ADDR,
				 DST_ADDR, DST_SIZE));
	ut_assert_nextline("Uncompressed size: %zd = 0x%zX", PLAIN_SIZE,
			   PLAIN_SIZE);
	ut_assert_console_end();

	/* the text comes back exactly, with nothing written beyond it */
	ut_asserteq_mem(plain, dst, PLAIN_SIZE);
	ut_asserteq(0, ((u8 *)dst)[PLAIN_SIZE]);

	/* the size is left in $filesize for the next command to use */
	ut_asserteq(PLAIN_SIZE, env_get_hex("filesize", 0));

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_unlz4_base, UTF_CONSOLE);

/* Test 'unlz4' with a destination too small to hold the result */
static int cmd_test_unlz4_small(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_frame(&dst);

	/*
	 * The size given is the room available at the destination, so a size
	 * below the uncompressed size stops the decompression rather than
	 * writing past the end of the buffer
	 */
	ut_asserteq(1, run_commandf("unlz4 %lx %lx %zx", SRC_ADDR,
				    DST_ADDR, PLAIN_SIZE / 2));
	ut_assert_nextline("Uncompressed err :-71");
	ut_assert_console_end();

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_unlz4_small, UTF_CONSOLE);

/* Test 'unlz4' on something which is not an lz4 frame */
static int cmd_test_unlz4_bad(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_frame(&dst);

	/* break the magic number, which the frame header is checked against */
	*(u8 *)src = 0;

	ut_asserteq(1, run_commandf("unlz4 %lx %lx %x", SRC_ADDR,
				    DST_ADDR, DST_SIZE));
	ut_assert_nextline("Uncompressed err :-93");
	ut_assert_console_end();

	/* nothing is written to the destination */
	ut_asserteq(0, *(u8 *)dst);

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_unlz4_bad, UTF_CONSOLE);

/* Test 'unlz4' with a bad command line */
static int cmd_test_unlz4_usage(struct unit_test_state *uts)
{
	/* all three arguments are required, so two is not enough */
	ut_asserteq(1, run_commandf("unlz4 %lx %lx", SRC_ADDR, DST_ADDR));
	ut_assert_nextline("unlz4 - lz4 uncompress a memory region");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextlinen("unlz4 srcaddr dstaddr dstsize");
	ut_assert_skip_to_line("");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_unlz4_usage, UTF_CONSOLE);
