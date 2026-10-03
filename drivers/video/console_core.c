// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2015
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 * (C) Copyright 2023 Dzmitry Sankouski <dsankouski@gmail.com>
 */

#include <video.h>
#include <video_console.h>
#include <dm.h>
#include <malloc.h>
#include <spl.h>
#include <video_font.h>
#include "vidconsole_internal.h"

/**
 * console_set_font() - prepare vidconsole for chosen font.
 *
 * @dev		vidconsole device
 * @ctx		vidconsole context
 * @fontdata	pointer to font data struct
 */
static int console_set_font(struct udevice *dev, struct vidconsole_ctx *ctx,
			    struct video_fontdata *fontdata)
{
	struct console_simple_priv *priv = dev_get_priv(dev);

	priv->fontdata = fontdata;
	vidconsole_set_bitmap_font(dev, ctx, fontdata);

	return 0;
}

int check_bpix_support(int bpix)
{
	if (bpix == VIDEO_BPP8 && IS_ENABLED(CONFIG_VIDEO_BPP8))
		return 0;
	else if (bpix == VIDEO_BPP16 && IS_ENABLED(CONFIG_VIDEO_BPP16))
		return 0;
	else if (bpix == VIDEO_BPP32 && IS_ENABLED(CONFIG_VIDEO_BPP32))
		return 0;
	else
		return -ENOSYS;
}

inline void fill_pixel_and_goto_next(void **dstp, u32 value, int pbytes, int step)
{
	u8 *dst_byte = *dstp;

	if (pbytes == 4) {
		u32 *dst = *dstp;
		*dst = value;
	}
	if (pbytes == 2) {
		u16 *dst = *dstp;
		*dst = value;
	}
	if (pbytes == 1) {
		u8 *dst = *dstp;
		*dst = value;
	}
	*dstp = dst_byte + step;
}

inline u32 swap_pixel_and_goto_next(void **dstp, u32 value, int pbytes, int step)
{
	u8 *dst_byte = *dstp;
	u32 old_value = 0;

	if (pbytes == 4) {
		u32 *dst = *dstp;
		old_value = *dst;
		*dst = value;
	}
	if (pbytes == 2) {
		u16 *dst = *dstp;
		old_value = *dst;
		*dst = value;
	}
	if (pbytes == 1) {
		u8 *dst = *dstp;
		old_value = *dst;
		*dst = value;
	}
	*dstp = dst_byte + step;

	return old_value;
}

int fill_char_vertically(uchar *pfont, void **line, struct video_priv *vid_priv,
			 struct video_fontdata *fontdata, bool direction)
{
	int step, line_step, pbytes, bitcount, width_remainder, ret;
	void *dst;

	ret = check_bpix_support(vid_priv->bpix);
	if (ret)
		return ret;

	pbytes = VNBYTES(vid_priv->bpix);
	if (direction) {
		step = -pbytes;
		line_step = -vid_priv->line_length;
	} else {
		step = pbytes;
		line_step = vid_priv->line_length;
	}

	width_remainder = fontdata->width % 8;
	for (int row = 0; row < fontdata->height; row++) {
		uchar bits;

		bitcount = 8;
		dst = *line;
		for (int col = 0; col < fontdata->byte_width; col++) {
			if (width_remainder) {
				bool is_last_col = (fontdata->byte_width - col == 1);

				if (is_last_col)
					bitcount = width_remainder;
			}
			bits = pfont[col];

			for (int bit = 0; bit < bitcount; bit++) {
				u32 value = (bits & 0x80) ?
					vid_priv->colour_fg :
					vid_priv->colour_bg;

				fill_pixel_and_goto_next(&dst,
							 value,
							 pbytes,
							 step
				);
				bits <<= 1;
			}
		}
		*line += line_step;
		pfont += fontdata->byte_width;
	}
	return ret;
}

int fill_char_horizontally(uchar *pfont, void **line, struct video_priv *vid_priv,
			   struct video_fontdata *fontdata, bool direction)
{
	int step, line_step, pbytes, bitcount = 8, width_remainder, ret;
	void *dst;
	u8 mask;

	ret = check_bpix_support(vid_priv->bpix);
	if (ret)
		return ret;

	pbytes = VNBYTES(vid_priv->bpix);
	if (direction) {
		step = -pbytes;
		line_step = vid_priv->line_length;
	} else {
		step = pbytes;
		line_step = -vid_priv->line_length;
	}

	width_remainder = fontdata->width % 8;
	for (int col = 0; col < fontdata->byte_width; col++) {
		mask = 0x80;
		if (width_remainder) {
			bool is_last_col = (fontdata->byte_width - col == 1);

			if (is_last_col)
				bitcount = width_remainder;
		}
		for (int bit = 0; bit < bitcount; bit++) {
			dst = *line;
			for (int row = 0; row < fontdata->height; row++) {
				u32 value = (pfont[row * fontdata->byte_width + col]
					     & mask) ? vid_priv->colour_fg : vid_priv->colour_bg;

				fill_pixel_and_goto_next(&dst,
							 value,
							 pbytes,
							 step
				);
			}
			*line += line_step;
			mask >>= 1;
		}
	}
	return ret;
}

int cursor_show(struct vidconsole_cursor *curs, struct video_priv *vid_priv,
		bool direction)
{
	int step, line_step, pbytes, ret, row;
	void *line, *dst;
	u32 *save_ptr;
	uint value;

	ret = check_bpix_support(vid_priv->bpix);
	if (ret)
		return ret;

	pbytes = VNBYTES(vid_priv->bpix);
	if (direction) {
		step = -pbytes;
		line_step = -vid_priv->line_length;
	} else {
		step = pbytes;
		line_step = vid_priv->line_length;
	}

	/* we should not already have saved data */
	if (curs->saved) {
		debug("Trying to show cursor but data is already saved\n");
		return -EINVAL;
	}

	/* Figure out where to write the cursor in the frame buffer */
	line = vid_priv->fb + curs->y * vid_priv->line_length +
		curs->x * VNBYTES(vid_priv->bpix);

	/* save pixels under cursor and draw new cursor in one pass */
	value = vid_priv->colour_fg;
	save_ptr = curs->save_data;
	for (row = 0; row < curs->height; row++) {
		dst = line;

		for (int col = 0; col < VIDCONSOLE_CURSOR_WIDTH; col++)
			*save_ptr++ = swap_pixel_and_goto_next(&dst, value,
							       pbytes, step);
		line += line_step;
	}
	curs->saved = true;

	return 0;
}

int cursor_hide(struct vidconsole_cursor *curs, struct video_priv *vid_priv,
		bool direction)
{
	int step, line_step, pbytes, ret;
	void *line, *dst;

	ret = check_bpix_support(vid_priv->bpix);
	if (ret)
		return ret;

	pbytes = VNBYTES(vid_priv->bpix);
	if (direction) {
		step = -pbytes;
		line_step = -vid_priv->line_length;
	} else {
		step = pbytes;
		line_step = vid_priv->line_length;
	}

	/* Trying to hide cursor - we should have saved data */
	if (!curs->saved) {
		debug("Trying to hide cursor but no data was saved\n");
		return -EINVAL;
	}

	/* Figure out where to write the cursor in the frame buffer */
	line = vid_priv->fb + curs->y * vid_priv->line_length +
		curs->x * VNBYTES(vid_priv->bpix);

	/* Restore saved pixels */
	u32 *save_ptr = curs->save_data;
	dst = line;
	for (int row = 0; row < curs->height; row++) {
		void *row_dst = dst;
		for (int col = 0; col < VIDCONSOLE_CURSOR_WIDTH; col++)
			fill_pixel_and_goto_next(&row_dst, *save_ptr++, pbytes,
						 step);
		dst += line_step;
	}
	curs->saved = false;

	return 0;
}

int console_alloc_cursor(struct udevice *dev, struct vidconsole_cursor *curs)
{
	struct video_priv *vid_priv;
	struct udevice *vid;
	int save_count;

	if (!CONFIG_IS_ENABLED(CONFIG_CURSOR) || xpl_phase() < PHASE_BOARD_R)
		return 0;
	vid = dev_get_parent(dev);
	vid_priv = dev_get_uclass_priv(vid);

	/* Allocate cursor save buffer for maximum possible cursor height */
	save_count = vid_priv->ysize * VIDCONSOLE_CURSOR_WIDTH;
	curs->save_data = malloc(save_count * sizeof(u32));
	if (!curs->save_data)
		return -ENOMEM;

	return 0;
}

void console_free_cursor(struct vidconsole_cursor *curs)
{
	free(curs->save_data);
}

int console_simple_ctx_new(struct udevice *dev, void *vctx)
{
	struct console_ctx *ctx = vctx;
	int ret;

	ret = console_set_font(dev, &ctx->com, fonts);
	if (ret)
		return ret;

	return 0;
}

const char *console_simple_get_font_size(struct udevice *dev, void *ctx,
					 uint *sizep)
{
	struct console_simple_priv *priv = dev_get_priv(dev);

	*sizep = priv->fontdata->width;

	return priv->fontdata->name;
}

int console_simple_get_font(struct udevice *dev, int seq, struct vidfont_info *info)
{
	info->name = fonts[seq].name;

	return info->name ? 0 : -ENOENT;
}

int console_fixed_putc_xy(struct udevice *dev, void *vctx, uint x_frac, uint y,
			  int cp, struct video_fontdata *fontdata)
{
	struct vidconsole_ctx *ctx = vctx;
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	int pbytes = VNBYTES(vid_priv->bpix);
	int x, linenum, ret;
	void *start, *line;
	u8 ch = console_utf_to_cp437(cp);
	uchar *pfont = fontdata->video_fontdata +
			ch * fontdata->char_pixel_bytes;

	if (x_frac + VID_TO_POS(ctx->x_charsize) > ctx->xsize_frac)
		return -EAGAIN;
	linenum = y;
	x = VID_TO_PIXEL(x_frac);
	start = vid_priv->fb + linenum * vid_priv->line_length + x * pbytes;
	line = start;

	ret = fill_char_vertically(pfont, &line, vid_priv, fontdata, NORMAL_DIRECTION);
	if (ret)
		return ret;

	video_damage(dev->parent,
		     x,
		     y,
		     fontdata->width,
		     fontdata->height);

	return VID_TO_POS(fontdata->width);
}

int console_simple_select_font(struct udevice *dev, void *ctx, const char *name,
			       uint size)
{
	struct video_fontdata *font;

	if (!name) {
		if (fonts->name)
			console_set_font(dev, ctx, fonts);
		return 0;
	}

	for (font = fonts; font->name; font++) {
		if (!strcmp(name, font->name)) {
			console_set_font(dev, ctx, font);
			return 0;
		}
	};
	printf("no such font: %s, make sure it's name has <width>x<height> format\n", name);
	return -ENOENT;
}
