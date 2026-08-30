// SPDX-License-Identifier: GPL-2.0+
/*
 * Tests for the bmp command
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <bmp_layout.h>
#include <command.h>
#include <console.h>
#include <dm.h>
#include <image.h>
#include <mapmem.h>
#include <video.h>
#include <test/cmd.h>
#include <test/ut.h>

/* Memory used by these tests */
#define BMP_ADDR	((ulong)CONFIG_SYS_LOAD_ADDR + 0x1000)

/* Size of the test image, in pixels */
#define BMP_WIDTH	4
#define BMP_HEIGHT	4

/* Number of palette entries in the test image */
#define BMP_COLOURS	2

/* Palette entry used by every pixel of the test image */
#define BMP_WHITE	1

/* Position used by the tests which draw somewhere other than the corner */
#define BMP_X		8
#define BMP_Y		12

/**
 * setup_bmp() - Write a small BMP image to memory
 *
 * The image is 4 x 4 pixels at 8 bits per pixel, with every pixel using the
 * white entry of its palette, so that a test can tell where it lands on the
 * display
 *
 * Return: pointer to the image
 */
static struct bmp_image *setup_bmp(void)
{
	struct bmp_color_table_entry *palette;
	uint data_offset, size;
	struct bmp_image *bmp;
	u8 *data;
	int i;

	data_offset = sizeof(struct bmp_header) +
		BMP_COLOURS * sizeof(struct bmp_color_table_entry);
	size = data_offset + BMP_WIDTH * BMP_HEIGHT;

	bmp = map_sysmem(BMP_ADDR, size);
	memset(bmp, '\0', size);

	bmp->header.signature[0] = 'B';
	bmp->header.signature[1] = 'M';
	bmp->header.file_size = cpu_to_le32(size);
	bmp->header.data_offset = cpu_to_le32(data_offset);

	/* the information header is the 40-byte version, which follows it */
	bmp->header.size = cpu_to_le32(40);
	bmp->header.width = cpu_to_le32(BMP_WIDTH);
	bmp->header.height = cpu_to_le32(BMP_HEIGHT);
	bmp->header.planes = cpu_to_le16(1);
	bmp->header.bit_count = cpu_to_le16(8);
	bmp->header.compression = cpu_to_le32(BMP_BI_RGB);
	bmp->header.image_size = cpu_to_le32(BMP_WIDTH * BMP_HEIGHT);
	bmp->header.colors_used = cpu_to_le32(BMP_COLOURS);

	palette = bmp->color_table;
	palette[BMP_WHITE].blue = 0xff;
	palette[BMP_WHITE].green = 0xff;
	palette[BMP_WHITE].red = 0xff;

	data = (u8 *)bmp + data_offset;
	for (i = 0; i < BMP_WIDTH * BMP_HEIGHT; i++)
		data[i] = BMP_WHITE;

	return bmp;
}

/**
 * clear_area() - Blank the part of the display an image is drawn into
 *
 * The area is one pixel larger than the image on each side, so that a test can
 * check that nothing outside the image is touched
 *
 * @uts: Test state
 * @x: X position of the image
 * @y: Y position of the image
 * Return: 0 if OK, -EAGAIN if there is no display, other -ve on error
 */
static int clear_area(struct unit_test_state *uts, int x, int y)
{
	struct video_priv *priv;
	struct udevice *dev;
	uint bytes;
	u8 *fb;
	int i;

	if (uclass_first_device_err(UCLASS_VIDEO, &dev))
		return -EAGAIN;
	priv = dev_get_uclass_priv(dev);
	bytes = VNBITS(priv->bpix) / 8;
	fb = priv->fb;

	/* an image in the corner has no border on two of its sides */
	for (i = max(-1, -y); i <= BMP_HEIGHT; i++) {
		int first = max(-1, -x);

		memset(fb + (y + i) * priv->line_length + (x + first) * bytes,
		       '\0', (BMP_WIDTH + 1 - first) * bytes);
	}

	return 0;
}

/**
 * check_area() - Check that the image is drawn where it is asked for
 *
 * Every pixel of the image is white, so each one must have become non-zero,
 * while the border around it must still be blank
 *
 * @uts: Test state
 * @x: X position of the image
 * @y: Y position of the image
 * Return: 0 if OK, -ve on error
 */
static int check_area(struct unit_test_state *uts, int x, int y)
{
	struct video_priv *priv;
	struct udevice *dev;
	uint bytes;
	u8 *fb;
	int i, j;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	priv = dev_get_uclass_priv(dev);
	bytes = VNBITS(priv->bpix) / 8;
	fb = priv->fb;

	for (i = max(-1, -y); i <= BMP_HEIGHT; i++) {
		for (j = max(-1, -x); j <= BMP_WIDTH; j++) {
			bool inside, blank;
			u8 *pix;
			uint k;

			pix = fb + (y + i) * priv->line_length +
				(x + j) * bytes;
			inside = i >= 0 && i < BMP_HEIGHT &&
				j >= 0 && j < BMP_WIDTH;

			for (blank = true, k = 0; k < bytes; k++)
				blank &= !pix[k];
			ut_assertf(blank != inside, "pixel %d, %d %s blank\n",
				   x + j, y + i, blank ? "is" : "is not");
		}
	}

	return 0;
}

/* Test 'bmp info' */
static int cmd_test_bmp_base(struct unit_test_state *uts)
{
	struct bmp_image *bmp;

	bmp = setup_bmp();

	ut_assertok(run_commandf("bmp info %lx", BMP_ADDR));
	ut_assert_nextline("Image size    : %d x %d", BMP_WIDTH, BMP_HEIGHT);
	ut_assert_nextline("Bits per pixel: 8");
	ut_assert_nextline("Compression   : 0");
	ut_assert_console_end();

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_base, UTF_CONSOLE);

/* Test 'bmp info' with no address, which uses the default load address */
static int cmd_test_bmp_default(struct unit_test_state *uts)
{
	struct bmp_image *bmp;
	ulong old;

	bmp = setup_bmp();

	old = image_load_addr;
	image_load_addr = BMP_ADDR;
	ut_assertok(run_command("bmp info", 0));
	image_load_addr = old;

	ut_assert_nextline("Image size    : %d x %d", BMP_WIDTH, BMP_HEIGHT);
	ut_assert_nextline("Bits per pixel: 8");
	ut_assert_nextline("Compression   : 0");
	ut_assert_console_end();

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_default, UTF_CONSOLE);

/* Test both sub-commands on memory which holds no image */
static int cmd_test_bmp_invalid(struct unit_test_state *uts)
{
	struct bmp_image *bmp;

	bmp = setup_bmp();

	/* the signature is all that tells the command it has an image */
	bmp->header.signature[1] = 'N';

	ut_asserteq(1, run_commandf("bmp info %lx", BMP_ADDR));
	ut_assert_nextline("There is no valid bmp file at the given address");
	ut_assert_console_end();

	ut_asserteq(1, run_commandf("bmp display %lx", BMP_ADDR));
	ut_assert_nextline("There is no valid bmp file at the given address");
	ut_assert_console_end();

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_invalid, UTF_CONSOLE);

/* Test 'bmp display' in the corner of the display */
static int cmd_test_bmp_display(struct unit_test_state *uts)
{
	struct bmp_image *bmp;
	int ret;

	bmp = setup_bmp();

	/* the image lands on the first video device, if there is one */
	ret = clear_area(uts, 0, 0);
	if (ret == -EAGAIN)
		return ret;
	ut_assertok(ret);

	ut_assertok(run_commandf("bmp display %lx", BMP_ADDR));
	ut_assert_console_end();
	ut_assertok(check_area(uts, 0, 0));

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_display, UTF_CONSOLE);

/* Test 'bmp display' at a position */
static int cmd_test_bmp_pos(struct unit_test_state *uts)
{
	struct bmp_image *bmp;
	int ret;

	bmp = setup_bmp();

	ret = clear_area(uts, BMP_X, BMP_Y);
	if (ret == -EAGAIN)
		return ret;
	ut_assertok(ret);

	/* the position is decimal, unlike the address */
	ut_assertok(run_commandf("bmp display %lx %d %d", BMP_ADDR, BMP_X,
				 BMP_Y));
	ut_assert_console_end();
	ut_assertok(check_area(uts, BMP_X, BMP_Y));

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_pos, UTF_CONSOLE);

/* Test 'bmp display' with a position which is off the display */
static int cmd_test_bmp_offscreen(struct unit_test_state *uts)
{
	struct video_priv *priv;
	struct bmp_image *bmp;
	struct udevice *dev;

	bmp = setup_bmp();

	if (uclass_first_device_err(UCLASS_VIDEO, &dev))
		return -EAGAIN;
	priv = dev_get_uclass_priv(dev);

	/* the command fails, and draw_bmp() prints nothing about it */
	ut_asserteq(1, run_commandf("bmp display %lx %d %d", BMP_ADDR,
				    priv->xsize, priv->ysize));
	ut_assert_console_end();

	unmap_sysmem(bmp);

	return 0;
}
CMD_TEST(cmd_test_bmp_offscreen, UTF_CONSOLE);

/* Test the usage errors */
static int cmd_test_bmp_usage(struct unit_test_state *uts)
{
	ut_asserteq(1, run_command("bmp", 0));
	ut_assert_skip_to_line("bmp display <imageAddr> [x y] - display image at x,y");
	ut_assert_console_end();

	ut_asserteq(1, run_command("bmp fish 100", 0));
	ut_assert_skip_to_line("bmp display <imageAddr> [x y] - display image at x,y");
	ut_assert_console_end();

	/* 'bmp info' takes at most one address */
	ut_asserteq(1, run_commandf("bmp info %lx %lx", BMP_ADDR, BMP_ADDR));
	ut_assert_skip_to_line("bmp display <imageAddr> [x y] - display image at x,y");
	ut_assert_console_end();

	return 0;
}
CMD_TEST(cmd_test_bmp_usage, UTF_CONSOLE);
