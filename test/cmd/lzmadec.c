// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the lzmadec command
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

/* The text which the stream below holds, from test/lib/compression.c */
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
 * An LZMA-alone stream holding the text above, made with
 * 'lzma -z -c plain.txt' and taken from test/lib/compression.c
 */
static const char lzma_stream[] =
	"\x5d\x00\x00\x80\x00\xff\xff\xff\xff\xff\xff\xff\xff\x00\x24\x88"
	"\x08\x26\xd8\x41\xff\x99\xc8\xcf\x66\x3d\x80\xac\xba\x17\xf1\xc8"
	"\xb9\xdf\x49\x37\xb1\x68\xa0\x2a\xdd\x63\xd1\xa7\xa3\x66\xf8\x15"
	"\xef\xa6\x67\x8a\x14\x18\x80\xcb\xc7\xb1\xcb\x84\x6a\xb2\x51\x16"
	"\xa1\x45\xa0\xd6\x3e\x55\x44\x8a\x5c\xa0\x7c\xe5\xa8\xbd\x04\x57"
	"\x8f\x24\xfd\xb9\x34\x50\x83\x2f\xf3\x46\x3e\xb9\xb0\x00\x1a\xf5"
	"\xd3\x86\x7e\x8f\x77\xd1\x5d\x0e\x7c\xe1\xac\xde\xf8\x65\x1f\x4d"
	"\xce\x7f\xa7\x3d\xaa\xcf\x26\xa7\x58\x69\x1e\x4c\xea\x68\x8a\xe5"
	"\x89\xd1\xdc\x4d\xc7\xe0\x07\x42\xbf\x0c\x9d\x06\xd7\x51\xa2\x0b"
	"\x7c\x83\x35\xe1\x85\xdf\xee\xfb\xa3\xee\x2f\x47\x5f\x8b\x70\x2b"
	"\xe1\x37\xf3\x16\xf6\x27\x54\x8a\x33\x72\x49\xea\x53\x7d\x60\x0b"
	"\x21\x90\x66\xe7\x9e\x56\x61\x5d\xd8\xdc\x59\xf0\xac\x2f\xd6\x49"
	"\x6b\x85\x40\x08\x1f\xdf\x26\x25\x3b\x72\x44\xb0\xb8\x21\x2f\xb3"
	"\xd7\x9b\x24\x30\x78\x26\x44\x07\xc3\x33\xd1\x4d\x03\x1b\xe1\xff"
	"\xfd\xf5\x50\x8d\xca";
#define STREAM_SIZE	(sizeof(lzma_stream) - 1)

/**
 * setup_stream() - Put the compressed stream in memory and clear the output
 *
 * @dstp: Returns a pointer to the destination region, which is zeroed
 * Return: pointer to the source region, holding the stream
 */
static void *setup_stream(void **dstp)
{
	void *src, *dst;

	src = map_sysmem(SRC_ADDR, STREAM_SIZE);
	memcpy(src, lzma_stream, STREAM_SIZE);

	dst = map_sysmem(DST_ADDR, DST_SIZE);
	memset(dst, '\0', DST_SIZE);
	*dstp = dst;

	return src;
}

/* Test 'lzmadec' decompressing a stream */
static int cmd_test_lzmadec_base(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_stream(&dst);

	ut_assertok(run_commandf("lzmadec %lx %lx", SRC_ADDR, DST_ADDR));
	ut_assert_nextline("Uncompressed size: %zd = 0X%zX", PLAIN_SIZE,
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
CMD_TEST(cmd_test_lzmadec_base, UTF_CONSOLE);

/* Test 'lzmadec' with room to spare at the destination */
static int cmd_test_lzmadec_size(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_stream(&dst);

	/* the optional size says how much room the destination has */
	ut_assertok(run_commandf("lzmadec %lx %lx %x", SRC_ADDR,
				 DST_ADDR, DST_SIZE));
	ut_assert_nextline("Uncompressed size: %zd = 0X%zX", PLAIN_SIZE,
			   PLAIN_SIZE);
	ut_assert_console_end();

	ut_asserteq_mem(plain, dst, PLAIN_SIZE);

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_lzmadec_size, UTF_CONSOLE);

/* Test 'lzmadec' with a destination too small to hold the result */
static int cmd_test_lzmadec_small(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_stream(&dst);

	/*
	 * The size given is the room available at the destination, so the
	 * decompression stops there and fails, rather than writing past the
	 * end of the buffer. The command says nothing about this and only the
	 * return value shows it
	 */
	ut_asserteq(1, run_commandf("lzmadec %lx %lx %zx", SRC_ADDR,
				    DST_ADDR, PLAIN_SIZE / 2));
	ut_assert_console_end();

	/* what was decompressed is correct, but nothing beyond it is touched */
	ut_asserteq_mem(plain, dst, PLAIN_SIZE / 2);
	ut_asserteq(0, ((u8 *)dst)[PLAIN_SIZE / 2]);

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_lzmadec_small, UTF_CONSOLE);

/* Test 'lzmadec' on something which is not an LZMA stream */
static int cmd_test_lzmadec_bad(struct unit_test_state *uts)
{
	void *src, *dst;

	src = setup_stream(&dst);

	/* break the properties byte, which no valid stream can hold */
	*(u8 *)src = 0xff;

	ut_asserteq(1, run_commandf("lzmadec %lx %lx", SRC_ADDR, DST_ADDR));
	ut_assert_console_end();

	unmap_sysmem(src);
	unmap_sysmem(dst);

	return 0;
}
CMD_TEST(cmd_test_lzmadec_bad, UTF_CONSOLE);

/* Test 'lzmadec' with a bad command line */
static int cmd_test_lzmadec_usage(struct unit_test_state *uts)
{
	/* both addresses are required, so one is not enough */
	ut_asserteq(1, run_commandf("lzmadec %lx", SRC_ADDR));
	ut_assert_nextline("lzmadec - lzma uncompress a memory region");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");
	ut_assert_nextline("lzmadec srcaddr dstaddr [dstsize]");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_lzmadec_usage, UTF_CONSOLE);
