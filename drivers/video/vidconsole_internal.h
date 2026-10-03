/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2015
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 * (C) Copyright 2023 Dzmitry Sankouski <dsankouski@gmail.com>
 */

#include <charset.h>
#include <config.h>
#include <video_console.h>

struct udevice;
struct vidconsole_cursor;
struct video_priv;

#define FLIPPED_DIRECTION 1
#define NORMAL_DIRECTION 0

/**
 * struct console_simple_priv - Private data for this driver
 *
 * @video_fontdata	font graphical representation data
 */
struct console_simple_priv {
	struct video_fontdata *fontdata;
};

/**
 * struct console_ctx - context for the normal console
 *
 * @com:	Common fields from the vidconsole uclass
 */
struct console_ctx {
	struct vidconsole_ctx com;
};

/**
 * Checks if bits per pixel supported.
 *
 * @param bpix	framebuffer bits per pixel.
 *
 * @returns 0, if supported, or else -ENOSYS.
 */
int check_bpix_support(int bpix);

/**
 * Fill 1 pixel in framebuffer, and go to next one.
 *
 * @param dstp		a pointer to pointer to framebuffer.
 * @param value		value to write to framebuffer.
 * @param pbytes	framebuffer bytes per pixel.
 * @param step		framebuffer pointer increment. Usually is equal to pbytes,
 *			and may be negative to control filling direction.
 */
void fill_pixel_and_goto_next(void **dstp, u32 value, int pbytes, int step);

/**
 * swap_pixel_and_goto_next() - Swap 1 pixel in framebuffer, and go to next one
 *
 * @param dstp		a pointer to pointer to framebuffer.
 * @param value		value to write to framebuffer.
 * @param pbytes	framebuffer bytes per pixel.
 * @param step		framebuffer pointer increment. Usually is equal to pbytes,
 *			and may be negative to control filling direction.
 * Return: old value of the pixel
 */
u32 swap_pixel_and_goto_next(void **dstp, u32 value, int pbytes, int step);

/**
 * Fills 1 character in framebuffer vertically. Vertically means we're filling char font data rows
 * across the lines.
 *
 * @param pfont		a pointer to character font data.
 * @param line		a pointer to pointer to framebuffer. It's a point for upper left char corner
 * @param vid_priv	driver private data.
 * @fontdata		font graphical representation data
 * @param direction	controls character orientation. Can be normal or flipped.
 * When normal:               When flipped:
 *|-----------------------------------------------|
 *| line stepping        |                        |
 *|            |         |       stepping ->      |
 *|     *      |         |       * * *            |
 *|   * *      v         |         *              |
 *|     *                |         *              |
 *|     *                |         * *      ^     |
 *|   * * *              |         *        |     |
 *|                      |                  |     |
 *| stepping ->          |         line stepping  |
 *|---!!we're starting from upper left char corner|
 *|-----------------------------------------------|
 *
 * @returns 0, if success, or else error code.
 */
int fill_char_vertically(uchar *pfont, void **line, struct video_priv *vid_priv,
			 struct video_fontdata *fontdata, bool direction);

/**
 * Fills 1 character in framebuffer horizontally.
 * Horizontally means we're filling char font data columns across the lines.
 *
 * @param pfont		a pointer to character font data.
 * @param line		a pointer to pointer to framebuffer. It's a point for upper left char corner
 * @param vid_priv	driver private data.
 * @fontdata		font graphical representation data
 * @param direction	controls character orientation. Can be normal or flipped.
 * When normal:               When flipped:
 *|-----------------------------------------------|
 *|               *        |   line stepping      |
 *|    ^  * * * * *        |   |                  |
 *|    |    *     *        |   v   *     *        |
 *|    |                   |       * * * * *      |
 *|  line stepping         |       *              |
 *|                        |                      |
 *|  stepping ->           |        <- stepping   |
 *|---!!we're starting from upper left char corner|
 *|-----------------------------------------------|
 *
 * @returns 0, if success, or else error code.
 */
int fill_char_horizontally(uchar *pfont, void **line, struct video_priv *vid_priv,
			   struct video_fontdata *fontdata, bool direction);

/**
 * cursor_show() - Show cursor by saving and drawing pixels
 *
 * @curs: cursor information
 * @vid_priv: video-device info
 * @direction: controls cursor orientation (normal or flipped)
 *
 * When normal:               When flipped:
 *|-----------------------------------------------|
 *|               *        |   line stepping      |
 *|    ^  * * * * *        |   |                  |
 *|    |    *     *        |   v   *     *        |
 *|    |                   |       * * * * *      |
 *|  line stepping         |       *              |
 *|                        |                      |
 *|  stepping ->           |        <<- stepping  |
 *|---!!we're starting from upper left char corner|
 *|-----------------------------------------------|
 *
 * Return: 0 on success, -EINVAL if cursor data already saved
 */
int cursor_show(struct vidconsole_cursor *curs, struct video_priv *vid_priv,
		bool direction);

/**
 * cursor_hide() - Hide cursor by restoring saved pixels
 *
 * @curs: cursor information
 * @vid_priv: video-device info
 * @direction: controls cursor orientation (normal or flipped)
 * Return: 0 if success, -EINVAL if no cursor data was saved
 */
int cursor_hide(struct vidconsole_cursor *curs, struct video_priv *vid_priv,
		bool direction);

/**
 * console_alloc_cursor() - Allocate cursor save buffer
 *
 * Allocates memory for saving pixels under the cursor
 *
 * @dev: vidconsole device
 * @curs: cursor to set up
 * Return: 0 if success, -ENOMEM if allocation fails
 */
int console_alloc_cursor(struct udevice *dev, struct vidconsole_cursor *curs);

/**
 * console_free_cursor() - Free cursor memory
 *
 * Free the memory used by a cursor
 *
 * @curs: cursor to set up
 */
void console_free_cursor(struct vidconsole_cursor *curs);

/**
 * console probe function.
 *
 * @param dev	a pointer to device.
 *
 * @returns 0, if success, or else error code.
 */
int console_probe(struct udevice *dev);

/**
 * Internal function to be used in as ops.
 * See details in video_console.h get_font_size function
 **/
const char *console_simple_get_font_size(struct udevice *dev, void *ctx,
					 uint *sizep);

/**
 * Internal function to be used in as ops.
 * See details in video_console.h get_font function
 **/
int console_simple_get_font(struct udevice *dev, int seq, struct vidfont_info *info);

/**
 * Internal function to be used in as ops.
 * See details in video_console.h select_font function
 **/
int console_simple_select_font(struct udevice *dev, void *ctx, const char *name,
			       uint size);

/**
 * Normal console putc_xy function that can be called by other console drivers
 *
 * @param dev		console device
 * @param ctx		vidconsole context to use, or NULL for default
 * @param x_frac	fractional X position
 * @param y		Y position in pixels
 * @param cp		Unicode code point
 * @returns width in fractional pixels, or -ve on error
 */
int console_normal_putc_xy(struct udevice *dev, void *ctx, uint x_frac,
			   uint y, int cp);

/**
 * Fixed font putc_xy function that can be called with explicit font data
 *
 * @param dev		console device
 * @param vctx		vidconsole context to use (cannot be NULL)
 * @param x_frac	fractional X position
 * @param y		Y position in pixels
 * @param cp		Unicode code point
 * @param fontdata	font data to use for rendering
 * @returns width in fractional pixels, or -ve on error
 */
int console_fixed_putc_xy(struct udevice *dev, void *vctx, uint x_frac, uint y,
			  int cp, struct video_fontdata *fontdata);

/**
 * Internal function to convert Unicode code points to code page 437.
 * Used by video consoles using bitmap fonts.
 *
 * @param codepoint	Unicode code point
 * @returns code page 437 character.
 */
static inline u8 console_utf_to_cp437(int codepoint)
{
	if (IS_ENABLED(CONFIG_CHARSET)) {
		utf_to_cp(&codepoint, codepage_437);
		return codepoint;
	}
	return codepoint;
}

/**
 * console_simple_ctx_new() - Set up a new context for bitmap-font consoles
 *
 * Initialises the context with the default bitmap font. This is the ctx_new()
 * method for bitmap-font console drivers.
 *
 * @dev: Vidconsole device
 * @ctx: Context to initialise (allocated by the uclass)
 * Return: 0 on success, -ve on error
 */
int console_simple_ctx_new(struct udevice *dev, void *ctx);
