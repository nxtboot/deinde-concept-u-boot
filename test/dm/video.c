// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2014 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <bmp_layout.h>
#include <bzlib.h>
#include <dm.h>
#include <gzip.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <os.h>
#include <video.h>
#include <video_console.h>
#include <asm/test.h>
#include <asm/sdl.h>
#include <dm/test.h>
#include <dm/uclass-internal.h>
#include <linux/delay.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>
#include <test/video.h>

/*
 * These tests use the standard sandbox frame buffer, the resolution of which
 * is defined in the device tree. This only supports 16bpp so the tests only
 * test that code path. It would be possible to adjust this fairly easily,
 * by adjusting the bpix value in struct sandbox_sdl_plat. However the code
 * in sandbox_sdl_sync() would also need to change to handle the different
 * surface depth.
 */
/* Basic test of the video uclass */
static int dm_test_video_base(struct unit_test_state *uts)
{
	struct video_priv *priv;
	struct udevice *dev;

	ut_assertok(uclass_get_device(UCLASS_VIDEO, 0, &dev));
	ut_asserteq(1366, video_get_xsize(dev));
	ut_asserteq(768, video_get_ysize(dev));
	priv = dev_get_uclass_priv(dev);
	ut_asserteq(priv->fb_size, 1366 * 768 * 2);

	return 0;
}
DM_TEST(dm_test_video_base, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/**
 * video_write_bmp() - Write framebuffer to BMP file
 *
 * This writes the current framebuffer contents to a BMP file on the host
 * filesystem. Useful for debugging video tests.
 *
 * @uts: Test state
 * @dev: Video device
 * @fname: Filename to write to
 * Return: 0 if OK, -ve on error
 */
static int video_write_bmp(struct unit_test_state *uts, struct udevice *dev,
			   const char *fname)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);
	struct bmp_image *bmp;
	u32 width = priv->xsize;
	u32 height = priv->ysize;
	u32 row_bytes, bmp_size, bpp, bytes_per_pixel;
	void *bmp_data;
	int ret, y;

	/* Support 16bpp (converted to 15bpp for BMP) and 32bpp */
	switch (priv->bpix) {
	case VIDEO_BPP16:
		bpp = 16;
		bytes_per_pixel = 2;
		break;
	case VIDEO_BPP32:
		bpp = 32;
		bytes_per_pixel = 4;
		break;
	default:
		return -ENOSYS;
	}

	/* BMP rows are padded to 4-byte boundary */
	row_bytes = ALIGN(width * bytes_per_pixel, BMP_DATA_ALIGN);
	bmp_size = sizeof(struct bmp_header) + row_bytes * height;

	bmp = malloc(bmp_size);
	if (!bmp)
		return -ENOMEM;

	memset(bmp, 0, bmp_size);

	/* Fill in BMP header */
	bmp->header.signature[0] = 'B';
	bmp->header.signature[1] = 'M';
	bmp->header.file_size = cpu_to_le32(bmp_size);
	bmp->header.data_offset = cpu_to_le32(sizeof(struct bmp_header));
	bmp->header.size = cpu_to_le32(40);
	bmp->header.width = cpu_to_le32(width);
	bmp->header.height = cpu_to_le32(height);
	bmp->header.planes = cpu_to_le16(1);
	bmp->header.bit_count = cpu_to_le16(bpp);
	bmp->header.compression = cpu_to_le32(BMP_BI_RGB);

	/* Copy framebuffer data (BMP is bottom-up) */
	bmp_data = (void *)bmp + sizeof(struct bmp_header);
	for (y = 0; y < height; y++) {
		void *src = priv->fb + (height - 1 - y) * priv->line_length;
		void *dst = bmp_data + y * row_bytes;

		if (bpp == 16) {
			/* Convert RGB565 to RGB555 for BMP format */
			u16 *src16 = (u16 *)src;
			u16 *dst16 = (u16 *)dst;
			int x;

			for (x = 0; x < width; x++) {
				u16 pixel = src16[x];
				/* Extract RGB565 components */
				u16 r = (pixel >> 11) & 0x1f;  /* 5 bits */
				u16 g = (pixel >> 5) & 0x3f;   /* 6 bits */
				u16 b = pixel & 0x1f;          /* 5 bits */
				/* Convert to RGB555: drop LSB of green */
				dst16[x] = (r << 10) | ((g >> 1) << 5) | b;
			}
		} else {
			memcpy(dst, src, width * bytes_per_pixel);
		}
	}

	ret = os_write_file(fname, bmp, bmp_size);
	free(bmp);

	return ret;
}

/**
 * save_video() - Save a portion of the framebuffer
 *
 * Saves the top portion of the framebuffer so it can be restored later.
 * The buffer is allocated on first use and stored in uts->video_save.
 *
 * @uts: Unit test state
 * @dev: Video device
 * @lines: Number of lines to save
 * Return: 0 on success, -ve on error
 */
static int save_video(struct unit_test_state *uts, struct udevice *dev,
		      uint lines)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);
	int size;

	size = priv->line_length * lines;
	if (size > priv->fb_size)
		return -EINVAL;

	/* Allocate/resize the save buffer to exact size needed */
	if (!abuf_realloc(&uts->video_save, size))
		return -ENOMEM;

	memcpy(abuf_data(&uts->video_save), priv->fb, size);

	return 0;
}

/**
 * restore_video() - Restore a saved portion of the framebuffer
 *
 * Restores the framebuffer region previously saved by save_video().
 *
 * @uts: Unit test state
 * @dev: Video device
 * Return: 0 on success, -ve on error
 */
static int restore_video(struct unit_test_state *uts, struct udevice *dev)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);

	if (!abuf_size(&uts->video_save))
		return -ENOENT;

	memcpy(priv->fb, abuf_data(&uts->video_save),
	       abuf_size(&uts->video_save));

	return 0;
}

/**
 * show_test_msg() - Display a test message at the top right of the screen
 *
 * @uts: Unit test state
 * @dev: Video device
 * @msg: Message to display
 * Return: true if framebuffer was saved and needs restoring, false otherwise
 */
static bool show_test_msg(struct unit_test_state *uts, struct udevice *dev,
			  const char *msg)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = uts->video_ctx;
	struct udevice *con;
	bool saved = false;
	int len = strlen(msg);
	int x, ret;

	ret = uclass_first_device_err(UCLASS_VIDEO_CONSOLE, &con);
	if (ret)
		return false;

	/* Allocate context on first use and select 8x16 font */
	if (!ctx) {
		ret = vidconsole_ctx_new(con, (void **)&ctx);
		if (ret)
			return false;
		uts->video_ctx = ctx;
		vidconsole_select_font(con, ctx, "8x16", 0);
	}

	/* Calculate position at top right */
	x = priv->xsize - (len + 2) * ctx->x_charsize;

	/* Save the affected region (one line) */
	if (!save_video(uts, dev, ctx->y_charsize))
		saved = true;

	/* Draw the message */
	vidconsole_set_cursor_pos(con, ctx, x, 0);
	vidconsole_put_string(con, ctx, msg);
	video_manual_sync(dev, VIDSYNC_COPY | VIDSYNC_FLUSH);

	return saved;
}

int video_compress_fb_(struct unit_test_state *uts, struct udevice *dev,
		       bool use_copy, const char *msg)
{
	struct sandbox_state *state = state_get_current();
	struct video_priv *priv = dev_get_uclass_priv(dev);
	uint destlen;
	void *dest;
	int ret;

	if (!IS_ENABLED(CONFIG_VIDEO_COPY))
		use_copy = false;

	destlen = priv->fb_size;
	dest = malloc(priv->fb_size);
	if (!dest)
		return -ENOMEM;
	ret = BZ2_bzBuffToBuffCompress(dest, &destlen,
				       use_copy ? priv->copy_fb : priv->fb,
				       priv->fb_size,
				       3, 0, 0);
	free(dest);
	if (ret)
		return ret;

	/* Write frame to file if --video-frames option is set */
	if (state->video_frames_dir) {
		char filename[256];

		snprintf(filename, sizeof(filename), "%s/frame%d.bmp",
			 state->video_frames_dir, state->video_frame_count++);
		ret = video_write_bmp(uts, dev, filename);
		if (ret) {
			ut_reportf("Failed to write frame to %s: %d",
				   filename, ret);
		}
	}

	/* provide a useful delay if -V flag is used or LOG_DEBUG is set */
	if (state->video_test || _DEBUG) {
		int delay = state->video_test ? state->video_test : 300;
		bool saved = false;

		if (msg)
			saved = show_test_msg(uts, dev, msg);

		mdelay(delay);

		/* Restore the framebuffer region */
		if (saved) {
			restore_video(uts, dev);
			video_manual_sync(dev, VIDSYNC_COPY | VIDSYNC_FLUSH);
		}
	}

	return destlen;
}

int video_compress_fb(struct unit_test_state *uts, struct udevice *dev,
		      bool use_copy)
{
	return video_compress_fb_(uts, dev, use_copy, NULL);
}

int video_check_copy_fb(struct unit_test_state *uts, struct udevice *dev)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);

	if (!IS_ENABLED(CONFIG_VIDEO_COPY))
		return 0;

	video_sync(dev, false);
	ut_assertf(!memcmp(priv->fb, priv->copy_fb, priv->fb_size),
		   "Copy framebuffer does not match fb");

	return 0;
}

int ut_check_video(struct unit_test_state *uts, const char *msg)
{
	struct udevice *dev;
	int ret;

	ret = uclass_first_device_err(UCLASS_VIDEO, &dev);
	if (ret)
		return ret;

	return video_compress_fb_(uts, dev, false, msg);
}

/*
 * Call this function at any point to halt and show the current display. Be
 * sure to run the test with the -l flag.
 */
static void __maybe_unused see_output(void)
{
	video_sync_all();
	while (1);
}

/* Select the video console driver to use for a video device */
static int select_vidconsole(struct unit_test_state *uts, const char *drv_name)
{
	struct sandbox_sdl_plat *plat;
	struct udevice *dev;

	ut_assertok(uclass_find_device(UCLASS_VIDEO, 0, &dev));
	ut_assert(!device_active(dev));
	plat = dev_get_plat(dev);
	plat->vidconsole_drv_name = "vidconsole0";

	return 0;
}

/**
 * video_get_nologo() - Disable the logo on the video device and return it
 *
 * @uts: Test state
 * @devp: Returns video device
 * Return: 0 if OK, -ve on error
 */
static int video_get_nologo(struct unit_test_state *uts, struct udevice **devp)
{
	struct video_uc_plat *uc_plat;
	struct udevice *dev;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	uc_plat = dev_get_uclass_plat(dev);
	uc_plat->hide_logo = true;

	/* now probe it */
	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	*devp = dev;

	return 0;
}

/* Test text output works on the video console */
static int dm_test_video_text(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	int i;

#define WHITE		0xffff
#define SCROLL_LINES	100

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_putc_xy(con, NULL, 0, 0, 'a');
	ut_asserteq(79, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	vidconsole_putc_xy(con, NULL, 0, 0, ' ');
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	for (i = 0; i < 20; i++)
		vidconsole_putc_xy(con, NULL, VID_TO_POS(i * 8), 0, ' ' + i);
	ut_asserteq(273, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	vidconsole_set_row(con, 0, WHITE);
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	for (i = 0; i < 20; i++)
		vidconsole_putc_xy(con, NULL, VID_TO_POS(i * 8), 0, ' ' + i);
	ut_asserteq(273, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_text, UTF_SCAN_PDATA | UTF_SCAN_FDT);

static int dm_test_video_text_12x22(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	int i;

#define WHITE		0xffff
#define SCROLL_LINES	100

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "12x22", 0));
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_putc_xy(con, NULL, 0, 0, 'a');
	ut_asserteq(89, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	vidconsole_putc_xy(con, NULL, 0, 0, ' ');
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	for (i = 0; i < 20; i++)
		vidconsole_putc_xy(con, NULL, VID_TO_POS(i * 8), 0, ' ' + i);
	ut_asserteq(363, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	vidconsole_set_row(con, 0, WHITE);
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	for (i = 0; i < 20; i++)
		vidconsole_putc_xy(con, NULL, VID_TO_POS(i * 8), 0, ' ' + i);
	ut_asserteq(363, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_text_12x22, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test handling of special characters in the console */
static int dm_test_video_chars(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	const char *test_string = "Well\b\b\b\bxhe is\r \n\ta very \amodest  "
		"\bman\n\t\tand Has much to\b\bto be modest about.";

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	vidconsole_put_string(con, NULL, test_string);
	ut_asserteq(466, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_chars, UTF_SCAN_PDATA | UTF_SCAN_FDT);

#ifdef CONFIG_VIDEO_ANSI
#define ANSI_ESC "\x1b"
/* Test handling of ANSI escape sequences */
static int dm_test_video_ansi(struct unit_test_state *uts)
{
	struct udevice *dev, *con;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));

	/* reference clear: */
	video_clear(con->parent);
	video_sync(con->parent, false);
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* test clear escape sequence: [2J */
	vidconsole_put_string(con, NULL, "A\tB\tC" ANSI_ESC "[2J");
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* test set-cursor: [%d;%df */
	vidconsole_put_string(con, NULL, "abc" ANSI_ESC "[2;2fab" ANSI_ESC "[4;4fcd");
	ut_asserteq(143, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* test colors (30-37 fg color, 40-47 bg color) */
	vidconsole_put_string(con, NULL, ANSI_ESC "[30;41mfoo"); /* black on red */
	vidconsole_put_string(con, NULL, ANSI_ESC "[33;44mbar"); /* yellow on blue */
	ut_asserteq(272, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_ansi, UTF_SCAN_PDATA | UTF_SCAN_FDT);
#endif

/**
 * check_vidconsole_output() - Run a text console test
 *
 * @uts:	Test state
 * @rot:	Console rotation (0=normal orientation, 1=90 degrees clockwise,
 *		2=upside down, 3=90 degree counterclockwise)
 * @wrap_size:	Expected size of compressed frame buffer for the wrap test
 * @scroll_size: Same for the scroll test
 * Return: 0 on success
 */
static int check_vidconsole_output(struct unit_test_state *uts, int rot,
				   int wrap_size, int scroll_size)
{
	struct udevice *dev, *con;
	struct sandbox_sdl_plat *plat;
	int i;

	ut_assertok(uclass_find_device(UCLASS_VIDEO, 0, &dev));
	ut_assert(!device_active(dev));
	plat = dev_get_plat(dev);
	plat->rot = rot;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* Check display wrap */
	for (i = 0; i < 120; i++)
		vidconsole_put_char(con, NULL, 'A' + i % 50);
	ut_asserteq(wrap_size, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* Check display scrolling */
	for (i = 0; i < SCROLL_LINES; i++) {
		vidconsole_put_char(con, NULL, 'A' + i % 50);
		vidconsole_put_char(con, NULL, '\n');
	}
	ut_asserteq(scroll_size, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* If we scroll enough, the screen becomes blank again */
	for (i = 0; i < SCROLL_LINES; i++)
		vidconsole_put_char(con, NULL, '\n');
	ut_asserteq(46, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}

/* Test text output through the console uclass */
static int dm_test_video_context(struct unit_test_state *uts)
{
	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(check_vidconsole_output(uts, 0, 788, 453));

	return 0;
}
DM_TEST(dm_test_video_context, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test rotated text output through the console uclass */
static int dm_test_video_rotation1(struct unit_test_state *uts)
{
	ut_assertok(check_vidconsole_output(uts, 1, 1112, 680));

	return 0;
}
DM_TEST(dm_test_video_rotation1, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test rotated text output through the console uclass */
static int dm_test_video_rotation2(struct unit_test_state *uts)
{
	ut_assertok(check_vidconsole_output(uts, 2, 783, 445));

	return 0;
}
DM_TEST(dm_test_video_rotation2, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test rotated text output through the console uclass */
static int dm_test_video_rotation3(struct unit_test_state *uts)
{
	ut_assertok(check_vidconsole_output(uts, 3, 1134, 681));

	return 0;
}
DM_TEST(dm_test_video_rotation3, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Read a file into memory and return a pointer to it */
static int read_file(struct unit_test_state *uts, const char *fname,
		     ulong *addrp)
{
	int buf_size = 100000;
	ulong addr = 0;
	int size, fd;
	char *buf;

	buf = map_sysmem(addr, 0);
	ut_assert(buf != NULL);
	fd = os_open(fname, OS_O_RDONLY);
	ut_assert(fd >= 0);
	size = os_read(fd, buf, buf_size);
	os_close(fd);
	ut_assert(size >= 0);
	ut_assert(size < buf_size);
	*addrp = addr;

	return 0;
}

/* Test drawing a bitmap file */
static int dm_test_video_bmp(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(1368, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a bitmap file on a 8bpp display */
static int dm_test_video_bmp8(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP8));

	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(1247, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp8, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a bitmap file on a 16bpp display */
static int dm_test_video_bmp16(struct unit_test_state *uts)
{
	ulong src, src_len = ~0UL;
	uint dst_len = ~0U;
	struct udevice *dev;
	ulong dst = 0x10000;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP16));

	ut_assertok(read_file(uts, "tools/logos/denx-16bpp.bmp.gz", &src));
	ut_assertok(gunzip(map_sysmem(dst, 0), dst_len, map_sysmem(src, 0),
			   &src_len));

	ut_assertok(video_bmp_display(dev, dst, 0, 0, false));
	ut_asserteq(3700, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp16, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a 24bpp bitmap file on a 16bpp display */
static int dm_test_video_bmp24(struct unit_test_state *uts)
{
	ulong src, src_len = ~0UL;
	uint dst_len = ~0U;
	struct udevice *dev;
	ulong dst = 0x10000;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP16));

	ut_assertok(read_file(uts, "tools/logos/denx-24bpp.bmp.gz", &src));
	ut_assertok(gunzip(map_sysmem(dst, 0), dst_len, map_sysmem(src, 0),
			   &src_len));

	ut_assertok(video_bmp_display(dev, dst, 0, 0, false));
	ut_asserteq(3656, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp24, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a 24bpp bitmap file on a 32bpp display */
static int dm_test_video_bmp24_32(struct unit_test_state *uts)
{
	ulong src, src_len = ~0UL;
	uint dst_len = ~0U;
	struct udevice *dev;
	ulong dst = 0x10000;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP32));

	ut_assertok(read_file(uts, "tools/logos/denx-24bpp.bmp.gz", &src));
	ut_assertok(gunzip(map_sysmem(dst, 0), dst_len, map_sysmem(src, 0),
			   &src_len));

	ut_assertok(video_bmp_display(dev, dst, 0, 0, false));
	ut_asserteq(6827, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp24_32, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a bitmap file on a 32bpp display */
static int dm_test_video_bmp32(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP32));
	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(2024, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp32, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a compressed bitmap file */
static int dm_test_video_bmp_comp(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(read_file(uts, "tools/logos/denx-comp.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(1368, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_bmp_comp, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a bitmap file on a 32bpp display */
static int dm_test_video_comp_bmp32(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP32));

	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(2024, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_comp_bmp32, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing a bitmap file on a 8bpp display */
static int dm_test_video_comp_bmp8(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong addr;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP8));

	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));

	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));
	ut_asserteq(1247, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_comp_bmp8, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/**
 * check_bmp_alpha() - Test drawing the riscos pointer with transparency
 *
 * Draws the riscos pointer BMP in various positions with and without
 * transparency to verify alpha blending works correctly
 *
 * @uts: Test state
 * @dev: Video device
 * @first: Expected compression after first pointer draw
 * @second: Expected compression after second pointer draw
 * @trans: Expected compression after drawing with transparency
 * Return: 0 if OK, -ve on error
 */
static int check_bmp_alpha(struct unit_test_state *uts, struct udevice *dev,
			   int first, int second, int trans)
{
	struct video_priv *priv = dev_get_uclass_priv(dev);
	ulong addr;

	addr = map_to_sysmem(video_image_getptr(riscos_arrow));

	/* Draw a black rectangle first */
	video_draw_box(dev, 100, 100, 200, 200, 0,
		       video_index_to_colour(priv, VID_BLACK), true);

	/* Draw the pointer on top of the black rectangle */
	ut_assertok(video_bmp_display(dev, addr, 110, 110, false));
	ut_asserteq(first, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* Draw the pointer on top of the white background */
	ut_assertok(video_bmp_display(dev, addr, 350, 110, false));
	ut_asserteq(second, video_compress_fb(uts, dev, false));

	/* Draw the pointer with white (0xffffff) as transparent */
	ut_assertok(video_bmp_displaya(dev, addr, 110, 160, false, true,
				       0xffffff));
	ut_assertok(video_bmp_displaya(dev, addr, 350, 160, false, true,
				       0xffffff));
	ut_asserteq(trans, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}

/* Test drawing the riscos pointer on a 16bpp display */
static int dm_test_video_bmp_alpha16(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(check_bmp_alpha(uts, dev, 174, 249, 358));

	return 0;
}
DM_TEST(dm_test_video_bmp_alpha16, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test drawing the riscos pointer on 32bpp display */
static int dm_test_video_bmp_alpha32(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	ut_assertok(sandbox_sdl_set_bpp(dev, VIDEO_BPP32));
	ut_assertok(check_bmp_alpha(uts, dev, 641, 710, 869));

	return 0;
}
DM_TEST(dm_test_video_bmp_alpha32, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test TrueType console */
static int dm_test_video_truetype(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	const char *test_string = "Criticism may not be agreeable, but it "
		"is necessary. It fulfils the same function as pain in the "
		"human body. It calls attention to an unhealthy state of "
		"things. Some see private enterprise as a predatory target to "
		"be shot, others as a cow to be milked, but few are those who "
		"see it as a sturdy horse pulling the wagon. The \aprice "
		"OF\b\bof greatness\n\tis responsibility.\n\nBye";

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_put_string(con, NULL, test_string);
	vidconsole_put_stringn(con, NULL, test_string, 30);
	ut_asserteq(13073, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_truetype, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test scrolling TrueType console */
static int dm_test_video_truetype_scroll(struct unit_test_state *uts)
{
	struct sandbox_sdl_plat *plat;
	struct udevice *dev, *con;
	const char *test_string = "Criticism may not be agreeable, but it "
		"is necessary. It fulfils the same function as pain in the "
		"human body. It calls attention to an unhealthy state of "
		"things. Some see private enterprise as a predatory target to "
		"be shot, others as a cow to be milked, but few are those who "
		"see it as a sturdy horse pulling the wagon. The \aprice "
		"OF\b\bof greatness\n\tis responsibility.\n\nBye";

	ut_assertok(uclass_find_device(UCLASS_VIDEO, 0, &dev));
	ut_assert(!device_active(dev));
	plat = dev_get_plat(dev);
	plat->font_size = 100;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_put_string(con, NULL, test_string);
	ut_asserteq(34248, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_truetype_scroll, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test TrueType backspace, within and across lines */
static int dm_test_video_truetype_bs(struct unit_test_state *uts)
{
	struct sandbox_sdl_plat *plat;
	struct udevice *dev, *con;
	const char *test_string = "...Criticism may or may\b\b\b\b\b\bnot be "
		"agreeable, but seldom it is necessary\b\b\b\b\b\b\b\b\b\b\b\b"
		"\b\b\b\b\b\b\b\b\b\bit is necessary. It fulfils the same "
		"function as pain in the human body. It calls attention to an "
		"unhealthy state of things.";

	ut_assertok(uclass_find_device(UCLASS_VIDEO, 0, &dev));
	ut_assert(!device_active(dev));
	plat = dev_get_plat(dev);
	plat->font_size = 100;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_put_string(con, NULL, test_string);
	ut_asserteq(29310, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_truetype_bs, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test partial rendering onto hardware frame buffer */
static int dm_test_video_copy(struct unit_test_state *uts)
{
	struct sandbox_sdl_plat *plat;
	struct video_uc_plat *uc_plat;
	struct udevice *dev, *con;
	struct video_priv *priv;
	const char *test_string = "\n\tCriticism may not be agreeable, but it "
		"is necessary.\t";
	ulong addr;

	if (!IS_ENABLED(CONFIG_VIDEO_COPY))
		return -EAGAIN;

	ut_assertok(uclass_find_first_device(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	uc_plat = dev_get_uclass_plat(dev);
	uc_plat->hide_logo = true;
	plat = dev_get_plat(dev);
	plat->font_size = 32;
	ut_assert(!device_active(dev));
	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));
	ut_assertnonnull(dev);
	priv = dev_get_uclass_priv(dev);

	ut_assertok(read_file(uts, "tools/logos/denx.bmp", &addr));
	ut_assertok(video_bmp_display(dev, addr, 0, 0, false));

	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_put_string(con, NULL, "\n\n\n\n\n");
	vidconsole_put_string(con, NULL, test_string);
	vidconsole_put_string(con, NULL, test_string);

	ut_asserteq(6884, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	/*
	 * Secretly clear the hardware frame buffer, but in a different
	 * color (black) to see which parts will be overwritten.
	 */
	memset(priv->copy_fb, 0, priv->fb_size);

	/*
	 * We should have the full content on the main buffer, but only
	 * 'damage' should have been copied to the copy buffer. This consists
	 * of a while rectangle with the Denx logo and four lines of text. The
	 * rest of the display is black.
	 *
	 * An easy way to try this is by changing video_sync() to call
	 * sandbox_sdl_sync(priv->copy_fb) instead of priv->fb then running the
	 * unit test:
	 *
	 *   ./u-boot -Tl
	 *   ut dm dm_test_video_copy
	 */
	vidconsole_put_string(con, NULL, test_string);
	vidconsole_put_string(con, NULL, test_string);
	video_sync(dev, true);
	ut_asserteq(7621, video_compress_fb(uts, dev, false));
	ut_asserteq(7741, video_compress_fb(uts, dev, true));

	return 0;
}
DM_TEST(dm_test_video_copy, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test video damage tracking */
static int dm_test_video_damage(struct unit_test_state *uts)
{
	struct sandbox_sdl_plat *plat;
	struct udevice *dev, *con;
	struct video_priv *priv;
	struct vid_bbox *damage;
	const char *test_string_1 = "Criticism may not be agreeable, ";
	const char *test_string_2 = "but it is necessary.";
	const char *test_string_3 = "It fulfils the same function as pain in "
		"the human body.";

	if (!IS_ENABLED(CONFIG_VIDEO_DAMAGE))
		return -EAGAIN;

	ut_assertok(uclass_find_device(UCLASS_VIDEO, 0, &dev));
	ut_assert(!device_active(dev));
	plat = dev_get_plat(dev);
	plat->font_size = 32;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	priv = dev_get_uclass_priv(dev);

	damage = &priv->damage;

	vidconsole_position_cursor(con, 14, 10);
	vidconsole_put_string(con, NULL, test_string_2);
	ut_asserteq(449, damage->x0);
	ut_asserteq(325, damage->y0);
	ut_asserteq(661, damage->x1);
	ut_asserteq(350, damage->y1);

	vidconsole_position_cursor(con, 7, 5);
	vidconsole_put_string(con, NULL, test_string_1);
	ut_asserteq(225, damage->x0);
	ut_asserteq(164, damage->y0);
	ut_asserteq(661, damage->x1);
	ut_asserteq(350, damage->y1);

	vidconsole_position_cursor(con, 21, 15);
	vidconsole_put_string(con, NULL, test_string_3);
	ut_asserteq(225, damage->x0);
	ut_asserteq(164, damage->y0);
	ut_asserteq(1280, damage->x1);
	ut_asserteq(510, damage->y1);

	video_sync(dev, true);
	ut_asserteq(priv->xsize, damage->x0);
	ut_asserteq(priv->ysize, damage->y0);
	ut_asserteq(0, damage->x1);
	ut_asserteq(0, damage->y1);

	ut_asserteq(7335, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_damage, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test font measurement */
static int dm_test_font_measure(struct unit_test_state *uts)
{
	const char *test_string = "There is always much\nto be said for not "
		"attempting more than you can do and for making a certainty of "
		"what you try. But this principle, like others in life and "
		"war, has its exceptions.";
	const struct vidconsole_mline *line;
	struct vidconsole_bbox bbox;
	struct video_priv *priv;
	struct udevice *dev, *con;
	const int limit = 0x320;
	struct alist lines;
	int nl;

	ut_assertok(uclass_get_device(UCLASS_VIDEO, 0, &dev));
	priv = dev_get_uclass_priv(dev);
	ut_asserteq(1366, priv->xsize);
	ut_asserteq(768, priv->ysize);

	/* this is using the Nimbus font with size of 18 pixels */
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	vidconsole_position_cursor(con, 0, 0);
	alist_init_struct(&lines, struct vidconsole_mline);
	ut_assertok(vidconsole_measure(con, NULL, 0, test_string, -1, -1, &bbox,
				       &lines));
	ut_asserteq(0, bbox.x0);
	ut_asserteq(0, bbox.y0);
	ut_asserteq(0x3ea, bbox.x1);
	ut_asserteq(0x24, bbox.y1);
	ut_asserteq(2, lines.count);

	nl = strchr(test_string, '\n') - test_string;

	line = alist_get(&lines, 0, struct vidconsole_mline);
	ut_assertnonnull(line);
	ut_asserteq(0, line->bbox.x0);
	ut_asserteq(0, line->bbox.y0);
	ut_asserteq(0x8c, line->bbox.x1);
	ut_asserteq(0x12, line->bbox.y1);
	ut_asserteq(0, line->start);
	ut_asserteq(20, line->len);
	ut_asserteq(nl, line->len);

	line++;
	ut_asserteq(0x0, line->bbox.x0);
	ut_asserteq(0x12, line->bbox.y0);
	ut_asserteq(0x3ea, line->bbox.x1);
	ut_asserteq(0x24, line->bbox.y1);
	ut_asserteq(21, line->start);
	ut_asserteq(nl + 1, line->start);
	ut_asserteq(163, line->len);
	ut_asserteq(strlen(test_string + nl + 1), line->len);

	/* now use a limit on the width */
	ut_assertok(vidconsole_measure(con, NULL, 0, test_string, -1, limit,
				       &bbox, &lines));
	ut_asserteq(0, bbox.x0);
	ut_asserteq(0, bbox.y0);
	ut_asserteq(0x31e, bbox.x1);
	ut_asserteq(0x36, bbox.y1);
	ut_asserteq(3, lines.count);

	nl = strchr(test_string, '\n') - test_string;

	line = alist_get(&lines, 0, struct vidconsole_mline);
	ut_assertnonnull(line);
	ut_asserteq(0, line->bbox.x0);
	ut_asserteq(0, line->bbox.y0);
	ut_asserteq(0x8c, line->bbox.x1);
	ut_asserteq(0x12, line->bbox.y1);
	ut_asserteq(0, line->start);
	ut_asserteq(20, line->len);
	ut_asserteq(nl, line->len);
	printf("line0 '%.*s'\n", line->len, test_string + line->start);
	ut_asserteq_strn("There is always much",
			 test_string + line->start);

	line++;
	ut_asserteq(0x0, line->bbox.x0);
	ut_asserteq(0x12, line->bbox.y0);
	ut_asserteq(0x31e, line->bbox.x1);
	ut_asserteq(0x24, line->bbox.y1);
	ut_asserteq(21, line->start);
	ut_asserteq(nl + 1, line->start);
	ut_asserteq(129, line->len);
	printf("line1 '%.*s'\n", line->len, test_string + line->start);
	ut_asserteq_strn("to be said for not attempting more than you can do "
			 "and for making a certainty of what you try. But this "
			 "principle, like others in",
			 test_string + line->start);

	line++;
	ut_asserteq(0x0, line->bbox.x0);
	ut_asserteq(0x24, line->bbox.y0);
	ut_asserteq(0xc8, line->bbox.x1);
	ut_asserteq(0x36, line->bbox.y1);
	ut_asserteq(21 + 130, line->start);
	ut_asserteq(33, line->len);
	printf("line2 '%.*s'\n", line->len, test_string + line->start);
	ut_asserteq_strn("life and war, has its exceptions.",
			 test_string + line->start);

	/*
	 * all characters should be accounted for, except the newline and the
	 * space which is consumed in the wordwrap
	 */
	ut_asserteq(strlen(test_string) - 2,
		    line[-2].len + line[-1].len + line->len);

	return 0;
}
DM_TEST(dm_test_font_measure, UTF_SCAN_FDT);

/* Test silencing the video console */
static int dm_test_video_silence(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	struct stdio_dev *sdev;

	ut_assertok(uclass_first_device_err(UCLASS_VIDEO, &dev));

	/*
	 * use the old console device from before when dm_test_pre_run() was
	 * called, since that is what is in stdio / console
	 */
	sdev = stdio_get_by_name("vidconsole");
	ut_assertnonnull(sdev);
	con = sdev->priv;
	/*
	 * Restore the default font so an earlier test that left a different
	 * one selected does not change the pixels written by the messages
	 * below.
	 */
	ut_assertok(vidconsole_select_font(con, NULL, NULL, 0));
	ut_assertok(vidconsole_clear_and_reset(con));
	ut_unsilence_console(uts);

	printf("message 1: console\n");
	vidconsole_put_string(con, NULL, "message 1: video\n");

	vidconsole_set_quiet(con, true);
	printf("second message: console\n");
	vidconsole_put_string(con, NULL, "second message: video\n");

	vidconsole_set_quiet(con, false);
	printf("final message: console\n");
	vidconsole_put_string(con, NULL, "final message: video\n");

	ut_asserteq(3944, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_silence, UTF_SCAN_FDT);

/* test drawing a box */
static int dm_test_video_box(struct unit_test_state *uts)
{
	struct video_priv *priv;
	struct udevice *dev;

	ut_assertok(video_get_nologo(uts, &dev));
	priv = dev_get_uclass_priv(dev);

	/* test outline boxes */
	video_draw_box(dev, 100, 100, 200, 200, 3,
		       video_index_to_colour(priv, VID_LIGHT_BLUE), false);
	video_draw_box(dev, 300, 100, 400, 200, 1,
		       video_index_to_colour(priv, VID_MAGENTA), false);
	video_draw_box(dev, 500, 100, 600, 200, 20,
		       video_index_to_colour(priv, VID_LIGHT_RED), false);
	ut_asserteq(133, video_compress_fb(uts, dev, false));

	/* test filled boxes */
	video_draw_box(dev, 150, 250, 200, 300, 0,
		       video_index_to_colour(priv, VID_GREEN), true);
	video_draw_box(dev, 350, 250, 400, 300, 0,
		       video_index_to_colour(priv, VID_YELLOW), true);
	ut_asserteq(175, video_compress_fb(uts, dev, false));

	return 0;
}
DM_TEST(dm_test_video_box, UTF_SCAN_FDT);

/* font switching between TrueType and bitmap fonts */
static int dm_test_video_font_switch(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	const char *truetype_text =
		"This is a long line of text written with TrueType font that "
		"should wrap to multiple lines to test the multi-line "
		"functionality properly. This is the second part of TrueType "
		"text that should also be long enough to wrap and test the "
		"line handling.";
	const char *bitmap_text =
		"Now this is bitmap font text that spans multiple lines and "
		"should be rendered with the standard 8x16 bitmap font instead "
		"of TrueType. More of the line of-bitmap text for testing "
		"purposes.";
	const char *final_truetype_text =
		"Finally back to TrueType font for this concluding multi-line "
		"text that demonstrates the font switching functionality "
		"working correctly.\nFinal line of TrueType text to complete "
		"the test.\n";

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));

	/* Start with TrueType font and write multi-line text */
	vidconsole_put_string(con, NULL, truetype_text);

	/* Switch to bitmap font */
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	vidconsole_put_string(con, NULL, bitmap_text);

	/* Switch back to TrueType font */
	ut_assertok(vidconsole_select_font(con, NULL, NULL, 0));
	vidconsole_put_string(con, NULL, final_truetype_text);

	ut_asserteq(14892, video_compress_fb(uts, dev, false));

	return 0;
}
DM_TEST(dm_test_video_font_switch, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* cursor backspace without artifacts */
static int check_cursor_backspace(struct unit_test_state *uts,
				  struct udevice *dev, struct udevice *con,
				  int exp_height)
{
	int with_a, with_cursor, after_backspace, after_idle, after_hide;
	struct vidconsole_priv *vc_priv = dev_get_uclass_priv(con);
	struct vidconsole_ctx *ctx = vidconsole_ctx_from_priv(vc_priv);
	struct vidconsole_cursor *curs = &ctx->curs;

	/* Output chars without cursor */
	ut_assert(!curs->visible);
	ut_assert(!curs->enabled);
	ut_assert(!curs->saved);
	ut_assert(!curs->height);
	ut_assertok(vidconsole_put_char(con, NULL, ' '));
	ut_assertok(vidconsole_put_char(con, NULL, 'a'));
	with_a = video_compress_fb(uts, dev, false);

	/* Show cursor at current position (after 'a') */
	ut_assertok(vidconsole_show_cursor(con, NULL));
	ut_assert(curs->visible);
	ut_assert(curs->saved);
	ut_asserteq(exp_height, curs->height);
	with_cursor = video_compress_fb(uts, dev, false);

	/* Enable the cursor so that backspace will move it */
	curs->enabled = true;

	/* Do backspace - the cursor will be hidden */
	ut_assertok(vidconsole_put_char(con, NULL, '\b'));
	ut_assert(!curs->visible);
	ut_assert(!curs->saved);
	after_backspace = video_compress_fb(uts, dev, false);
	ut_asserteq(with_a, after_backspace);
	ut_assert(curs->enabled);

	/* Run idle function - this should show the cursor */
	vidconsole_idle(con);
	ut_assert(curs->visible);
	ut_assert(curs->saved);
	after_idle = video_compress_fb(uts, dev, false);
	ut_assert(after_idle != with_a);

	/* Hide the cursor */
	ut_assertok(vidconsole_hide_cursor(con, NULL));
	ut_assert(curs->enabled);
	ut_assert(!curs->visible);
	ut_assert(!curs->saved);
	after_hide = video_compress_fb(uts, dev, false);

	ut_asserteq(with_a, after_hide);

	return 0;
}

/* cursor backspace without artifacts */
static int dm_test_video_backspace_normal(struct unit_test_state *uts)
{
	struct udevice *dev, *con;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	ut_assertok(check_cursor_backspace(uts, dev, con, 16));

	return 0;
}
DM_TEST(dm_test_video_backspace_normal, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* cursor backspace without artifacts on truetype */
static int dm_test_video_backspace_truetype(struct unit_test_state *uts)
{
	struct udevice *dev, *con;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, NULL, 30));
	ut_assertok(check_cursor_backspace(uts, dev, con, 30));

	return 0;
}
DM_TEST(dm_test_video_backspace_truetype, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* video commands */
static int dm_test_video_cmd(struct unit_test_state *uts)
{
	struct udevice *dev, *con;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));

	ut_assertok(run_command("setcurs 10 5", 0));

	ut_assertok(run_command("lcdputs \"Test string\"", 0));
	ut_asserteq(187, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	ut_assertok(run_command("video setcursor 0 0", 0));
	ut_assertok(run_command("video puts \"Top left\"", 0));
	ut_asserteq(272, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	ut_assertok(run_command(
		"video write 14:6 \"Multi\" 19:7 \"Write\"", 0));
	ut_asserteq(381, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	ut_assertok(run_command("video write -p a3:34 \"Pixels\"", 0));
	ut_asserteq(440, video_compress_fb(uts, dev, false));
	ut_assertok(video_check_copy_fb(uts, dev));

	return 0;
}
DM_TEST(dm_test_video_cmd, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* video images command */
static int dm_test_video_images(struct unit_test_state *uts)
{
	ut_assertok(run_command("video images", 0));
	ut_assert_nextline("Name                       Size");
	ut_assert_nextline("-------------------- ----------");
	ut_assert_nextline("bgrt                      43926");
	ut_assert_nextline("canonical                 14258");
	ut_assert_nextline("deinde                     6354");
	ut_assert_nextline("help                       1782");
	ut_assert_nextline("lock                       2454");
	ut_assert_nextline("multipass                  4378");
	ut_assert_nextline("riscos_arrow               3798");
	ut_assert_nextline("settings                   1782");
	ut_assert_nextline("tick                        822");
	ut_assert_skip_to_line("");
	ut_assert_nextline("Total images: 9");
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_video_images, UTF_SCAN_PDATA | UTF_SCAN_FDT | UTF_CONSOLE);

/* Test manual-sync mode suppresses auto-sync */
static int dm_test_video_manual_sync(struct unit_test_state *uts)
{
	struct video_priv *priv;
	struct udevice *dev, *con;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	priv = dev_get_uclass_priv(dev);

	/* Write some text and verify it appears in the framebuffer */
	vidconsole_put_string(con, NULL, "Test");
	ut_asserteq(118, video_compress_fb(uts, dev, false));

	/* Sync to copy buffer before enabling manual-sync mode */
	ut_assertok(video_sync(dev, true));

	/* Enable manual-sync mode - sync should be suppressed */
	video_set_manual_sync(true);

	/* Clear and write new text - auto-sync should not happen */
	video_clear(dev);
	vidconsole_put_string(con, NULL, "Manual Sync");

	/* should do nothing in manual-sync mode */
	ut_assertok(video_sync(dev, false));

	/* The copy framebuffer should still show old content */
	if (IS_ENABLED(CONFIG_VIDEO_COPY)) {
		ut_assertf(memcmp(priv->fb, priv->copy_fb, priv->fb_size),
			   "Copy fb should not match fb in manual-sync mode");
	}

	/*
	 * video_sync() with force=true should still do nothing, except of
	 * course that without a copy framebuffer the string will be present on
	 * (only) framebuffer
	 */
	ut_assertok(video_sync(dev, true));
	if (IS_ENABLED(CONFIG_VIDEO_COPY)) {
		ut_asserteq(118, video_compress_fb(uts, dev, true));
		ut_assertf(memcmp(priv->fb, priv->copy_fb, priv->fb_size),
			   "Copy fb should not match fb in manual-sync mode");
	} else {
		ut_asserteq(183, video_compress_fb(uts, dev, true));
	}

	/* Now test video_manual_sync() directly with VIDSYNC_FORCE and COPY */
	ut_assertok(video_manual_sync(dev, VIDSYNC_FORCE | VIDSYNC_FLUSH |
	VIDSYNC_COPY));
	ut_asserteq(183, video_compress_fb(uts, dev, false));

	/* The copy framebuffer should now match since we forced the sync */
	ut_assertok(video_check_copy_fb(uts, dev));

	/* Write new text again */
	vidconsole_put_string(con, NULL, "Test2");

	/* without VIDSYNC_FLUSH or COPY - should do nothing */
	ut_assertok(video_manual_sync(dev, 0));

	/* Copy fb should not match since neither flush nor copy occurred */
	if (IS_ENABLED(CONFIG_VIDEO_COPY)) {
		ut_assertf(memcmp(priv->fb, priv->copy_fb, priv->fb_size),
			   "Copy fb shouldn't match fb w/o VIDSYNC_FLUSH/COPY");
	}

	/* video_manual_sync() with full flags - should perform full sync */
	ut_assertok(video_manual_sync(dev, VIDSYNC_FLUSH | VIDSYNC_COPY));
	ut_assertok(video_check_copy_fb(uts, dev));

	/* Disable manual-sync mode */
	video_set_manual_sync(false);

	return 0;
}
DM_TEST(dm_test_video_manual_sync, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that sync() receives the correct damage rectangle */
static int dm_test_video_sync_damage(struct unit_test_state *uts)
{
	struct vid_bbox damage;
	struct udevice *dev, *con;
	struct video_priv *priv;

	if (!IS_ENABLED(CONFIG_VIDEO_DAMAGE))
		return -EAGAIN;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));
	priv = dev_get_uclass_priv(dev);

	/* Use manual sync to prevent interference with the test */
	video_set_manual_sync(true);

	/* Clear the display - this creates a full-screen damage and syncs */
	video_clear(dev);
	ut_assertok(video_manual_sync(dev, VIDSYNC_FLUSH | VIDSYNC_COPY));
	ut_asserteq(46, video_compress_fb(uts, dev, false));

	/* Get the damage rectangle that was passed to sync() */
	ut_assertok(sandbox_sdl_get_sync_damage(dev, &damage));

	/* Should be the full screen */
	ut_assert(vid_bbox_valid(&damage));
	ut_asserteq(0, damage.x0);
	ut_asserteq(0, damage.y0);
	ut_asserteq(priv->xsize, damage.x1);
	ut_asserteq(priv->ysize, damage.y1);

	/* Sync again with no changes - should have empty damage */
	ut_assertok(video_manual_sync(dev, VIDSYNC_FLUSH | VIDSYNC_COPY));
	ut_assertok(sandbox_sdl_get_sync_damage(dev, &damage));
	ut_assert(!vid_bbox_valid(&damage));

	/* Check that priv->damage is still reset to empty */
	ut_assert(!vid_bbox_valid(&priv->damage));

	/* Write a small piece of text at a specific position */
	vidconsole_putc_xy(con, NULL, VID_TO_POS(400), 67, 'T');

	/* Check priv->damage before sync - should have text damage */
	ut_assert(vid_bbox_valid(&priv->damage));
	ut_asserteq(400, priv->damage.x0);
	ut_asserteq(67, priv->damage.y0);
	ut_asserteq(400 + 8, priv->damage.x1);  /* 8x16 font */
	ut_asserteq(67 + 16, priv->damage.y1);

	ut_assertok(video_manual_sync(dev, VIDSYNC_FLUSH | VIDSYNC_COPY));

	/* Get the damage rectangle that was passed to sync() */
	ut_assertok(sandbox_sdl_get_sync_damage(dev, &damage));

	/* The damage should cover just the character */
	ut_assert(vid_bbox_valid(&damage));
	ut_asserteq(400, damage.x0);
	ut_asserteq(67, damage.y0);
	ut_asserteq(400 + 8, damage.x1);
	ut_asserteq(67 + 16, damage.y1);

	/* Check priv->damage after sync - should be reset to empty */
	ut_assert(!vid_bbox_valid(&priv->damage));

	/* Draw a filled box at a different position */
	ut_assertok(video_draw_box(dev, 200, 300, 250, 340, 1, 0xffffff, true));

	/* Check priv->damage before sync - should have box damage */
	ut_assert(vid_bbox_valid(&priv->damage));
	ut_asserteq(200, priv->damage.x0);
	ut_asserteq(300, priv->damage.y0);
	ut_asserteq(250, priv->damage.x1);
	ut_asserteq(340, priv->damage.y1);

	ut_assertok(video_manual_sync(dev, VIDSYNC_FLUSH | VIDSYNC_COPY));

	/* Get the damage rectangle for the box */
	ut_assertok(sandbox_sdl_get_sync_damage(dev, &damage));

	/* The damage should cover the box area */
	ut_assert(vid_bbox_valid(&damage));
	ut_asserteq(200, damage.x0);
	ut_asserteq(300, damage.y0);
	ut_asserteq(250, damage.x1);
	ut_asserteq(340, damage.y1);

	/* Check priv->damage after sync - should be reset to inverted/empty */
	ut_assert(!vid_bbox_valid(&priv->damage));

	/* Restore auto-sync so later tests see the default behaviour */
	video_set_manual_sync(false);

	return 0;
}
DM_TEST(dm_test_video_sync_damage, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test vidconsole context allocation */
static int dm_test_video_context_alloc(struct unit_test_state *uts)
{
	struct udevice *dev, *con;
	void *ctx;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));

	ut_assertok(vidconsole_ctx_new(con, &ctx));
	ut_assertnonnull(ctx);

	ut_assertok(vidconsole_ctx_dispose(con, ctx));

	/* Dispose should handle NULL gracefully */
	ut_assertok(vidconsole_ctx_dispose(con, NULL));

	return 0;
}
DM_TEST(dm_test_video_context_alloc, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that two vidconsole contexts are independent */
static int dm_test_video_context_indep(struct unit_test_state *uts)
{
	struct vidconsole_ctx *ctx1, *ctx2;
	struct udevice *dev, *con;

	ut_assertok(select_vidconsole(uts, "vidconsole0"));
	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, "8x16", 0));

	/* Create two contexts */
	ut_assertok(vidconsole_ctx_new(con, (void **)&ctx1));
	ut_assertok(vidconsole_ctx_new(con, (void **)&ctx2));

	/* Set different cursor positions in each context */
	vidconsole_set_cursor_pos(con, ctx1, 100, 50);
	vidconsole_set_cursor_pos(con, ctx2, 200, 100);

	/* Verify positions are independent */
	ut_asserteq(VID_TO_POS(100), ctx1->xcur_frac);
	ut_asserteq(50, ctx1->ycur);
	ut_asserteq(VID_TO_POS(200), ctx2->xcur_frac);
	ut_asserteq(100, ctx2->ycur);

	/* Write to ctx1, verify ctx2 is unchanged */
	vidconsole_put_string(con, ctx1, "Hello");
	ut_asserteq(VID_TO_POS(200), ctx2->xcur_frac);
	ut_asserteq(100, ctx2->ycur);

	/* Write to ctx2, verify it moves independently */
	vidconsole_put_string(con, ctx2, "World");
	ut_assert(ctx2->xcur_frac > VID_TO_POS(200));
	ut_asserteq(100, ctx2->ycur);

	/* ctx1 should have moved from the first write */
	ut_assert(ctx1->xcur_frac > VID_TO_POS(100));
	ut_asserteq(50, ctx1->ycur);

	ut_assertok(vidconsole_ctx_dispose(con, ctx1));
	ut_assertok(vidconsole_ctx_dispose(con, ctx2));

	return 0;
}
DM_TEST(dm_test_video_context_indep, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test multiple contexts with truetype font */
static int dm_test_video_context_indep_tt(struct unit_test_state *uts)
{
	struct vidconsole_ctx *ctx1, *ctx2;
	struct udevice *dev, *con;
	int ctx1_x, ctx2_x;

	ut_assertok(video_get_nologo(uts, &dev));
	ut_assertok(uclass_get_device(UCLASS_VIDEO_CONSOLE, 0, &con));
	ut_assertok(vidconsole_select_font(con, NULL, NULL, 30));

	/* Create two contexts */
	ut_assertok(vidconsole_ctx_new(con, (void **)&ctx1));
	ut_assertok(vidconsole_ctx_new(con, (void **)&ctx2));

	/* Set different cursor positions */
	vidconsole_set_cursor_pos(con, ctx1, 50, 100);
	vidconsole_set_cursor_pos(con, ctx2, 300, 200);

	/* Write different text to each */
	vidconsole_put_string(con, ctx1, "First");
	ctx1_x = ctx1->xcur_frac;

	vidconsole_put_string(con, ctx2, "Second context");
	ctx2_x = ctx2->xcur_frac;

	/* Verify they moved independently */
	ut_asserteq(ctx1_x, ctx1->xcur_frac);  /* ctx1 unchanged by ctx2 write */
	ut_asserteq(100, ctx1->ycur);
	ut_asserteq(ctx2_x, ctx2->xcur_frac);
	ut_asserteq(200, ctx2->ycur);

	/* ctx2 should have moved more (longer string) */
	ut_assert(ctx2_x - VID_TO_POS(300) > ctx1_x - VID_TO_POS(50));

	ut_assertok(vidconsole_ctx_dispose(con, ctx1));
	ut_assertok(vidconsole_ctx_dispose(con, ctx2));

	return 0;
}
DM_TEST(dm_test_video_context_indep_tt, UTF_SCAN_PDATA | UTF_SCAN_FDT);
