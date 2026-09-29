// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the aes command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <malloc.h>
#include <mapmem.h>
#include <uboot_aes.h>
#include <test/cmd.h>
#include <test/ut.h>

/* number of blocks the tests encrypt, enough to exercise CBC chaining */
#define NUM_BLOCKS	2

/* number of bytes that comes to */
#define TEST_LEN	(NUM_BLOCKS * AES_BLOCK_LENGTH)

/* the last line of the help text, which ends the usage message */
#define USAGE_LAST \
	"                             The $iv must be 16 bytes long."

/*
 * Test vectors from NIST SP 800-38A, appendix F. Using published vectors rather
 * than a round trip of our own makes the tests notice a wrong key length or a
 * wrong mode, both of which round-trip perfectly well.
 */

/* AES-128 key, shared by examples F.1.1 and F.2.1 */
static const u8 key128[AES128_KEY_LENGTH] = {
	0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
	0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
};

/* AES-256 key, shared by examples F.1.5 and F.2.5 */
static const u8 key256[AES256_KEY_LENGTH] = {
	0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
	0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
	0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
	0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4,
};

/* initialisation vector the CBC examples use */
static const u8 iv_test[AES_BLOCK_LENGTH] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

/* first two plaintext blocks, the same in every example */
static const u8 plain[TEST_LEN] = {
	0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
	0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
	0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
	0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
};

/* F.2.1, CBC with key128 and iv_test */
static const u8 cbc128[TEST_LEN] = {
	0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
	0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
	0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
	0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
};

/* F.2.5, CBC with key256 and iv_test */
static const u8 cbc256[TEST_LEN] = {
	0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
	0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6,
	0x9c, 0xfc, 0x4e, 0x96, 0x7e, 0xdb, 0x80, 0x8d,
	0x67, 0x9f, 0x77, 0x7b, 0xc6, 0x70, 0x2c, 0x7d,
};

/* F.1.1, ECB with key128 */
static const u8 ecb128[TEST_LEN] = {
	0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
	0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
	0xf5, 0xd3, 0xd5, 0x85, 0x03, 0xb9, 0x69, 0x9d,
	0xe7, 0x85, 0x89, 0x5a, 0x96, 0xfd, 0xba, 0xaf,
};

/* F.1.5, ECB with key256 */
static const u8 ecb256[TEST_LEN] = {
	0xf3, 0xee, 0xd1, 0xbd, 0xb5, 0xd2, 0xa0, 0x3c,
	0x06, 0x4b, 0x5a, 0x7e, 0x3d, 0xb1, 0x81, 0xf8,
	0x59, 0x1c, 0xcb, 0x10, 0xd4, 0x10, 0xed, 0x26,
	0xdc, 0x5b, 0xa7, 0x4a, 0x31, 0x36, 0x28, 0x70,
};

/**
 * struct aes_buf - buffers the command operates on, with their addresses
 *
 * The buffers are allocated rather than placed at a fixed address, so that the
 * tests assume nothing about where a board has memory it may write to.
 *
 * @key: Key, with room for the longest one
 * @iv: Initialisation vector
 * @src: Plaintext to encrypt
 * @dst: Where the ciphertext goes
 * @out: Where the plaintext recovered from @dst goes
 * @key_addr: Address of @key, as the command wants it
 * @iv_addr: Address of @iv
 * @src_addr: Address of @src
 * @dst_addr: Address of @dst
 * @out_addr: Address of @out
 */
struct aes_buf {
	u8 key[AES256_KEY_LENGTH];
	u8 iv[AES_BLOCK_LENGTH];
	u8 src[TEST_LEN];
	u8 dst[TEST_LEN];
	u8 out[TEST_LEN];
	ulong key_addr;
	ulong iv_addr;
	ulong src_addr;
	ulong dst_addr;
	ulong out_addr;
};

/**
 * setup_buf() - Allocate the buffers and fill in the test vector
 *
 * @uts: Test state
 * @bufp: Returns the allocated buffers, to be freed by the caller
 * @key: Key to write to the key buffer
 * @key_len: Length of @key in bytes
 * Return: 0 if OK, other value on error
 */
static int setup_buf(struct unit_test_state *uts, struct aes_buf **bufp,
		     const u8 *key, uint key_len)
{
	struct aes_buf *buf;

	buf = calloc(1, sizeof(*buf));
	ut_assertnonnull(buf);

	memcpy(buf->key, key, key_len);
	memcpy(buf->iv, iv_test, sizeof(buf->iv));
	memcpy(buf->src, plain, sizeof(buf->src));

	buf->key_addr = map_to_sysmem(buf->key);
	buf->iv_addr = map_to_sysmem(buf->iv);
	buf->src_addr = map_to_sysmem(buf->src);
	buf->dst_addr = map_to_sysmem(buf->dst);
	buf->out_addr = map_to_sysmem(buf->out);

	*bufp = buf;

	return 0;
}

/**
 * check_usage() - Check that the command printed its usage message
 *
 * The help text lists every sub-command over thirty-odd lines, so this checks
 * the heading and skips to the last line rather than repeating all of it.
 *
 * @uts: Test state
 * Return: 0 if OK, other value on error
 */
static int check_usage(struct unit_test_state *uts)
{
	ut_assert_nextline("aes - AES 128/192/256 operations");
	ut_assert_nextline_empty();
	ut_assert_nextline("Usage:");

	/*
	 * The cbc enc and cbc dec entries end with the same line, so it is the
	 * second one which reaches the end of the help text
	 */
	ut_assert_skip_to_line(USAGE_LAST);
	ut_assert_skip_to_line(USAGE_LAST);
	ut_assert_console_end();

	return 0;
}

/* Test the CBC form which takes its key from memory */
static int cmd_test_aes_base(struct unit_test_state *uts)
{
	struct aes_buf *buf;

	ut_assertok(setup_buf(uts, &buf, key128, AES128_KEY_LENGTH));

	ut_assertok(run_commandf("aes enc %lx %lx %lx %lx %x", buf->key_addr,
				 buf->iv_addr, buf->src_addr, buf->dst_addr,
				 TEST_LEN));
	ut_asserteq_mem(cbc128, buf->dst, TEST_LEN);
	ut_assert_console_end();

	ut_assertok(run_commandf("aes dec %lx %lx %lx %lx %x", buf->key_addr,
				 buf->iv_addr, buf->dst_addr, buf->out_addr,
				 TEST_LEN));
	ut_asserteq_mem(plain, buf->out, TEST_LEN);
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_aes_base, UTF_CONSOLE);

/*
 * Test that the command-name suffix chooses the key length. A 256-bit key
 * silently used as a 128-bit one still round-trips, so only a published vector
 * shows the difference.
 */
static int cmd_test_aes_keylen(struct unit_test_state *uts)
{
	struct aes_buf *buf;

	ut_assertok(setup_buf(uts, &buf, key256, AES256_KEY_LENGTH));

	ut_assertok(run_commandf("aes.256 enc %lx %lx %lx %lx %x",
				 buf->key_addr, buf->iv_addr, buf->src_addr,
				 buf->dst_addr, TEST_LEN));
	ut_asserteq_mem(cbc256, buf->dst, TEST_LEN);
	ut_assert_console_end();

	ut_assertok(run_commandf("aes.256 dec %lx %lx %lx %lx %x",
				 buf->key_addr, buf->iv_addr, buf->dst_addr,
				 buf->out_addr, TEST_LEN));
	ut_asserteq_mem(plain, buf->out, TEST_LEN);
	ut_assert_console_end();

	/*
	 * The same bytes, used as a 128-bit key, must give a different answer,
	 * since only the first 16 of them take part
	 */
	ut_assertok(run_commandf("aes.128 enc %lx %lx %lx %lx %x",
				 buf->key_addr, buf->iv_addr, buf->src_addr,
				 buf->dst_addr, TEST_LEN));
	ut_assert(memcmp(cbc256, buf->dst, TEST_LEN));
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_aes_keylen, UTF_CONSOLE);

/**
 * check_slot_cipher() - Check a cipher which takes its key from a key slot
 *
 * Loads @key into slot 0, selects it, encrypts the test plaintext and checks
 * the result against @expect, then decrypts that back to the plaintext.
 *
 * @uts: Test state
 * @cbc: true for the cbc sub-command, false for ecb
 * @suffix: Suffix giving the key length, such as ".256"
 * @key: Key to load into the slot
 * @key_len: Length of @key in bytes
 * @expect: Expected ciphertext, TEST_LEN bytes long
 * Return: 0 if OK, other value on error
 */
static int check_slot_cipher(struct unit_test_state *uts, bool cbc,
			     const char *suffix, const u8 *key, uint key_len,
			     const u8 *expect)
{
	struct aes_buf *buf;

	ut_assertok(setup_buf(uts, &buf, key, key_len));

	ut_assertok(run_commandf("aes%s set_key %lx 0", suffix, buf->key_addr));
	ut_assertok(run_commandf("aes%s select_slot 0", suffix));
	ut_assert_console_end();

	if (cbc)
		ut_assertok(run_commandf("aes cbc enc %lx %lx %lx %x",
					 buf->iv_addr, buf->src_addr,
					 buf->dst_addr, TEST_LEN));
	else
		ut_assertok(run_commandf("aes ecb enc %lx %lx %x",
					 buf->src_addr, buf->dst_addr,
					 TEST_LEN));
	ut_asserteq_mem(expect, buf->dst, TEST_LEN);
	ut_assert_console_end();

	if (cbc)
		ut_assertok(run_commandf("aes cbc dec %lx %lx %lx %x",
					 buf->iv_addr, buf->dst_addr,
					 buf->out_addr, TEST_LEN));
	else
		ut_assertok(run_commandf("aes ecb dec %lx %lx %x",
					 buf->dst_addr, buf->out_addr,
					 TEST_LEN));
	ut_asserteq_mem(plain, buf->out, TEST_LEN);
	ut_assert_console_end();

	free(buf);

	return 0;
}

/* Test the ecb sub-command, which uses a key held in a slot */
static int cmd_test_aes_ecb(struct unit_test_state *uts)
{
	ut_assertok(check_slot_cipher(uts, false, "", key128,
				      AES128_KEY_LENGTH, ecb128));
	ut_assertok(check_slot_cipher(uts, false, ".256", key256,
				      AES256_KEY_LENGTH, ecb256));

	return 0;
}
CMD_TEST(cmd_test_aes_ecb, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test the cbc sub-command, which uses a key held in a slot */
static int cmd_test_aes_cbc(struct unit_test_state *uts)
{
	ut_assertok(check_slot_cipher(uts, true, "", key128,
				      AES128_KEY_LENGTH, cbc128));
	ut_assertok(check_slot_cipher(uts, true, ".256", key256,
				      AES256_KEY_LENGTH, cbc256));

	return 0;
}
CMD_TEST(cmd_test_aes_cbc, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test the key slots the device offers, and asking for one it does not have */
static int cmd_test_aes_slots(struct unit_test_state *uts)
{
	struct aes_buf *buf;

	ut_assertok(setup_buf(uts, &buf, key128, AES128_KEY_LENGTH));

	ut_assertok(run_command("aes get_slots", 0));
	ut_assert_nextline("Available slots: 2");
	ut_assert_console_end();

	/* the software engine has slots 0 and 1, so 9 is beyond it */
	ut_asserteq(1, run_commandf("aes set_key %lx 9", buf->key_addr));
	ut_assert_nextline("Unable to set key at slot: 1");
	ut_assert_console_end();

	ut_asserteq(1, run_command("aes select_slot 9", 0));
	ut_assert_nextline("Unable to select key slot: 1");
	ut_assert_console_end();

	/* with no slot selected there is no key to encrypt with */
	ut_asserteq(1, run_commandf("aes ecb enc %lx %lx %x", buf->src_addr,
				    buf->dst_addr, TEST_LEN));
	ut_assert_nextline("Unable to do ecb operation: 1");
	ut_assert_console_end();

	ut_asserteq(1, run_commandf("aes cbc enc %lx %lx %lx %x", buf->iv_addr,
				    buf->src_addr, buf->dst_addr, TEST_LEN));
	ut_assert_nextline("Unable to do cbc operation: 1");
	ut_assert_console_end();

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_aes_slots, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);

/* Test the command lines the aes command refuses */
static int cmd_test_aes_usage(struct unit_test_state *uts)
{
	struct aes_buf *buf;

	ut_assertok(setup_buf(uts, &buf, key128, AES128_KEY_LENGTH));

	/* a sub-command is needed */
	ut_asserteq(1, run_command("aes", 0));
	ut_assertok(check_usage(uts));

	/* and it must be one the command knows */
	ut_asserteq(1, run_command("aes frobnicate", 0));
	ut_assertok(check_usage(uts));

	/* the CBC form wants five addresses */
	ut_asserteq(1, run_commandf("aes enc %lx %lx %lx", buf->key_addr,
				    buf->iv_addr, buf->src_addr));
	ut_assertok(check_usage(uts));

	/* ecb and cbc want a direction of their own */
	ut_asserteq(1, run_commandf("aes ecb frobnicate %lx %lx %x",
				    buf->src_addr, buf->dst_addr, TEST_LEN));
	ut_assertok(check_usage(uts));

	ut_asserteq(1, run_commandf("aes cbc frobnicate %lx %lx %lx %x",
				    buf->iv_addr, buf->src_addr, buf->dst_addr,
				    TEST_LEN));
	ut_assertok(check_usage(uts));

	free(buf);

	return 0;
}
CMD_TEST(cmd_test_aes_usage, UTF_CONSOLE | UTF_DM | UTF_SCAN_FDT);
