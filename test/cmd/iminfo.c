// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the iminfo command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <console.h>
#include <image.h>
#include <mapmem.h>
#include <u-boot/crc.h>
#include <linux/libfdt.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define IMI_ADDR	((ulong)CONFIG_SYS_LOAD_ADDR + 0x1000)

/* Payload of the image, chosen so that the size is printed as 64 Bytes */
#define IMI_DATA_LEN	0x40

/* Values recorded in the header, so that the test can check what is printed */
#define IMI_LOAD	0x12345678
#define IMI_ENTRY	0x1234567c
#define IMI_NAME	"test image"

/* Values used for the FIT */
#define IMI_FIT_SIZE		0x400
#define IMI_FIT_DESC		"test FIT"
#define IMI_FIT_IMAGE_DESC	"test kernel"

/**
 * setup_image() - Write a valid legacy image to memory
 *
 * The payload is a byte ramp, so that a change to any of it alters the CRC
 *
 * Return: pointer to the image header
 */
static struct legacy_img_hdr *setup_image(void)
{
	struct legacy_img_hdr *hdr;
	u8 *data;
	int i;

	/*
	 * Clear twice the space the image needs, so that the region just after
	 * it is known to hold nothing which looks like an image
	 */
	hdr = map_sysmem(IMI_ADDR, 2 * (sizeof(*hdr) + IMI_DATA_LEN));
	memset(hdr, '\0', 2 * (sizeof(*hdr) + IMI_DATA_LEN));

	data = (u8 *)(hdr + 1);
	for (i = 0; i < IMI_DATA_LEN; i++)
		data[i] = i;

	image_set_magic(hdr, IH_MAGIC);
	image_set_time(hdr, 0);
	image_set_size(hdr, IMI_DATA_LEN);
	image_set_load(hdr, IMI_LOAD);
	image_set_ep(hdr, IMI_ENTRY);
	image_set_dcrc(hdr, crc32(0, data, IMI_DATA_LEN));
	image_set_os(hdr, IH_OS_LINUX);
	image_set_arch(hdr, IH_ARCH_SANDBOX);
	image_set_type(hdr, IH_TYPE_KERNEL);
	image_set_comp(hdr, IH_COMP_NONE);
	image_set_name(hdr, IMI_NAME);

	/* the header CRC covers the header with its own CRC field left zero */
	image_set_hcrc(hdr, crc32(0, (u8 *)hdr, sizeof(*hdr)));

	return hdr;
}

/**
 * check_contents() - Check the header fields printed for the test image
 *
 * @uts: Test state
 * Return: 0 if OK, -ve on error
 */
static int check_contents(struct unit_test_state *uts)
{
	ut_assert_nextline("   Image Name:   " IMI_NAME);
	if (IMAGE_ENABLE_TIMESTAMP)
		ut_assert_nextlinen("   Created:      ");
	ut_assert_nextline("   Image Type:   Sandbox Linux Kernel Image (uncompressed)");
	ut_assert_nextline("   Data Size:    64 Bytes = 64 Bytes");
	ut_assert_nextline("   Load Address: %08x", IMI_LOAD);
	ut_assert_nextline("   Entry Point:  %08x", IMI_ENTRY);

	return 0;
}

/* Test 'iminfo' on a valid image */
static int cmd_test_iminfo_base(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;

	hdr = setup_image();

	ut_assertok(run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   Legacy image found");
	ut_assertok(check_contents(uts));
	ut_assert_nextline("   Verifying Checksum ... OK");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_base, UTF_CONSOLE);

/* Test 'iminfo' with no address, which uses the default load address */
static int cmd_test_iminfo_default(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;
	ulong old;

	hdr = setup_image();

	old = image_load_addr;
	image_load_addr = IMI_ADDR;
	ut_assertok(run_command("iminfo", 0));
	image_load_addr = old;

	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   Legacy image found");
	ut_assertok(check_contents(uts));
	ut_assert_nextline("   Verifying Checksum ... OK");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_default, UTF_CONSOLE);

/* Test 'iminfo' on memory which holds no image */
static int cmd_test_iminfo_none(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;

	hdr = setup_image();

	/* without a magic number there is nothing to recognise */
	image_set_magic(hdr, 0);

	ut_asserteq(1, run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("Unknown image format!");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_none, UTF_CONSOLE);

/* Test 'iminfo' on an image whose header has been damaged */
static int cmd_test_iminfo_hcrc(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;

	hdr = setup_image();

	/*
	 * Change a field without updating the header CRC. The magic number is
	 * still there, so the image is recognised and then rejected
	 */
	image_set_load(hdr, IMI_LOAD + 1);

	ut_asserteq(1, run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   Legacy image found");
	ut_assert_nextline("   Bad Header Checksum");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_hcrc, UTF_CONSOLE);

/* Test 'iminfo' on an image whose payload has been damaged */
static int cmd_test_iminfo_dcrc(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;
	u8 *data;

	hdr = setup_image();

	/* the header is untouched, so the damage shows up only in the payload */
	data = (u8 *)(hdr + 1);
	data[IMI_DATA_LEN - 1] ^= 0xff;

	ut_asserteq(1, run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   Legacy image found");
	ut_assertok(check_contents(uts));
	ut_assert_nextline("   Verifying Checksum ...    Bad Data CRC");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_dcrc, UTF_CONSOLE);

/* Test 'iminfo' with more than one address */
static int cmd_test_iminfo_list(struct unit_test_state *uts)
{
	struct legacy_img_hdr *hdr;
	ulong second;

	hdr = setup_image();
	second = IMI_ADDR + sizeof(*hdr) + IMI_DATA_LEN;

	/*
	 * Each address is checked in turn and a failure part-way through does
	 * not stop the rest, but it does set the return value
	 */
	ut_asserteq(1, run_commandf("iminfo %lx %lx", second, IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", second);
	ut_assert_nextline("Unknown image format!");
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   Legacy image found");
	ut_assertok(check_contents(uts));
	ut_assert_nextline("   Verifying Checksum ... OK");
	ut_assert_console_end();

	unmap_sysmem(hdr);

	return 0;
}
CMD_TEST(cmd_test_iminfo_list, UTF_CONSOLE);

/**
 * setup_fit() - Write a small FIT to memory
 *
 * The FIT holds a single kernel image with a payload of IMI_DATA_LEN bytes and
 * a crc32 hash over it. It is built here with libfdt, so that the test needs no
 * image file and no mkimage
 *
 * @uts: Test state
 * @addr: Address to write the FIT to
 * @corrupt: true to damage the payload after the hash is recorded, so that
 *	verification fails
 * Return: 0 if OK, -ve on error
 */
static int setup_fit(struct unit_test_state *uts, ulong addr, bool corrupt)
{
	u8 data[IMI_DATA_LEN];
	int images, node, hash;
	void *fit;
	int i;

	for (i = 0; i < IMI_DATA_LEN; i++)
		data[i] = i;

	fit = map_sysmem(addr, IMI_FIT_SIZE);
	ut_assertok(fdt_create_empty_tree(fit, IMI_FIT_SIZE));
	ut_assertok(fdt_setprop_string(fit, 0, FIT_DESC_PROP, IMI_FIT_DESC));
	ut_assertok(fdt_setprop_u32(fit, 0, FIT_TIMESTAMP_PROP, 0));

	images = fdt_add_subnode(fit, 0, "images");
	ut_assert(images > 0);

	node = fdt_add_subnode(fit, images, "kernel");
	ut_assert(node > 0);
	ut_assertok(fdt_setprop_string(fit, node, FIT_DESC_PROP,
				       IMI_FIT_IMAGE_DESC));
	ut_assertok(fdt_setprop(fit, node, FIT_DATA_PROP, data, IMI_DATA_LEN));
	ut_assertok(fdt_setprop_string(fit, node, FIT_TYPE_PROP, "kernel"));
	ut_assertok(fdt_setprop_string(fit, node, FIT_ARCH_PROP, "sandbox"));
	ut_assertok(fdt_setprop_string(fit, node, FIT_OS_PROP, "linux"));
	ut_assertok(fdt_setprop_string(fit, node, FIT_COMP_PROP, "none"));
	ut_assertok(fdt_setprop_u32(fit, node, FIT_LOAD_PROP, IMI_LOAD));
	ut_assertok(fdt_setprop_u32(fit, node, FIT_ENTRY_PROP, IMI_ENTRY));

	/*
	 * The value is stored big-endian, which is what fdt_setprop_u32() does
	 * and what crc32_wd_buf() produces when the hash is checked
	 */
	hash = fdt_add_subnode(fit, node, FIT_HASH_NODENAME "-1");
	ut_assert(hash > 0);
	ut_assertok(fdt_setprop_string(fit, hash, FIT_ALGO_PROP, "crc32"));
	ut_assertok(fdt_setprop_u32(fit, hash, FIT_VALUE_PROP,
				    crc32(0, data, IMI_DATA_LEN)));

	if (corrupt) {
		u8 *ptr;
		int len;

		ptr = (u8 *)fdt_getprop_w(fit, node, FIT_DATA_PROP, &len);
		ut_assertnonnull(ptr);
		ut_asserteq(IMI_DATA_LEN, len);
		ptr[0] ^= 0xff;
	}

	ut_assertok(fdt_pack(fit));
	unmap_sysmem(fit);

	return 0;
}

/**
 * check_fit_contents() - Check the FIT information which iminfo prints
 *
 * @uts: Test state
 * Return: 0 if OK, -ve on error
 */
static int check_fit_contents(struct unit_test_state *uts)
{
	ut_assert_nextline("   FIT description: %s", IMI_FIT_DESC);
	ut_assert_nextline("   Created:         1970-01-01   0:00:00 UTC");
	ut_assert_nextline("    Image 0 (kernel)");
	ut_assert_nextline("     Description:   %s", IMI_FIT_IMAGE_DESC);
	ut_assert_nextline("     Created:       1970-01-01   0:00:00 UTC");
	ut_assert_nextline("     Type:          Kernel Image");
	ut_assert_nextline("     Compression:   uncompressed");

	/* the offset of the data depends on how libfdt lays out the tree */
	ut_assert_nextlinen("     Data Start:    0x");
	ut_assert_nextline("     Data Size:     64 Bytes = 64 Bytes");
	ut_assert_nextline("     Architecture:  Sandbox");
	ut_assert_nextline("     OS:            Linux");
	ut_assert_nextline("     Load Address:  0x%08x", IMI_LOAD);
	ut_assert_nextline("     Entry Point:   0x%08x", IMI_ENTRY);
	ut_assert_nextline("     Hash algo:     crc32");
	ut_assert_nextlinen("     Hash value:    ");

	return 0;
}

/* Test 'iminfo' on a FIT, rather than a legacy image */
static int cmd_test_iminfo_fit(struct unit_test_state *uts)
{
	/* without FIT support the command reports an unknown format */
	if (!IS_ENABLED(CONFIG_FIT))
		return -EAGAIN;

	ut_assertok(setup_fit(uts, IMI_ADDR, false));

	ut_assertok(run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   FIT image found");
	ut_assertok(check_fit_contents(uts));

	ut_assert_nextline("## Checking hash(es) for FIT Image at %08lx ...",
			   IMI_ADDR);
	ut_assert_nextline("   Hash(es) for Image 0 (kernel): crc32+ ");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_iminfo_fit, UTF_CONSOLE);

/* Test 'iminfo' on a FIT whose payload does not match its hash */
static int cmd_test_iminfo_fit_bad(struct unit_test_state *uts)
{
	/* without FIT support the command reports an unknown format */
	if (!IS_ENABLED(CONFIG_FIT))
		return -EAGAIN;

	ut_assertok(setup_fit(uts, IMI_ADDR, true));

	ut_asserteq(1, run_commandf("iminfo %lx", IMI_ADDR));
	ut_assert_nextline_empty();
	ut_assert_nextline("## Checking Image at %08lx ...", IMI_ADDR);
	ut_assert_nextline("   FIT image found");
	ut_assertok(check_fit_contents(uts));
	ut_assert_nextline("## Checking hash(es) for FIT Image at %08lx ...",
			   IMI_ADDR);
	ut_assert_nextline("   Hash(es) for Image 0 (kernel): crc32 error!");
	ut_assert_nextline("Bad hash value for 'hash-1' hash node in 'kernel' image node");
	ut_assert_nextline("Bad hash in FIT image!");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_iminfo_fit_bad, UTF_CONSOLE);
