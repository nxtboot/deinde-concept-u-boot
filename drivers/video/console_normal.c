// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2015
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 * (C) Copyright 2023 Dzmitry Sankouski <dsankouski@gmail.com>
 */

#include <charset.h>
#include <dm.h>
#include <malloc.h>
#include <spl.h>
#include <video.h>
#include <video_console.h>
#include <video_font.h>		/* Get font data, width and height */
#include "vidconsole_internal.h"


static int console_set_row(struct udevice *dev, uint row, int clr)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_simple_priv *priv = dev_get_priv(dev);
	struct video_fontdata *fontdata = priv->fontdata;
	void *line, *dst, *end;
	int pixels = fontdata->height * vid_priv->xsize;
	int ret;
	int i;
	int pbytes;

	ret = check_bpix_support(vid_priv->bpix);
	if (ret)
		return ret;

	line = vid_priv->fb + row * fontdata->height * vid_priv->line_length;
	dst = line;
	pbytes = VNBYTES(vid_priv->bpix);
	for (i = 0; i < pixels; i++)
		fill_pixel_and_goto_next(&dst, clr, pbytes, pbytes);
	end = dst;

	video_damage(dev->parent,
		     0,
		     fontdata->height * row,
		     vid_priv->xsize,
		     fontdata->height);

	return 0;
}

static int console_move_rows(struct udevice *dev, uint rowdst,
			     uint rowsrc, uint count)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_simple_priv *priv = dev_get_priv(dev);
	struct video_fontdata *fontdata = priv->fontdata;
	void *dst;
	void *src;
	int size;

	dst = vid_priv->fb + rowdst * fontdata->height * vid_priv->line_length;
	src = vid_priv->fb + rowsrc * fontdata->height * vid_priv->line_length;
	size = fontdata->height * vid_priv->line_length * count;
	memmove(dst, src, size);

	video_damage(dev->parent,
		     0,
		     fontdata->height * rowdst,
		     vid_priv->xsize,
		     fontdata->height * count);

	return 0;
}

int console_normal_putc_xy(struct udevice *dev, void *vctx, uint x_frac,
			   uint y, int cp)
{
	struct console_simple_priv *priv = dev_get_priv(dev);

	return console_fixed_putc_xy(dev, vctx, x_frac, y, cp, priv->fontdata);
}

static __maybe_unused int console_get_cursor_info(struct udevice *dev,
						  void *vctx)
{
	struct vidconsole_ctx *ctx = vctx;
	struct console_simple_priv *priv = dev_get_priv(dev);
	struct video_fontdata *fontdata = priv->fontdata;
	struct vidconsole_cursor *curs = &ctx->curs;
	int x, y, index, xspace, xpos;

	/* for now, this is not used outside expo */
	if (!IS_ENABLED(CONFIG_EXPO))
		return -ENOSYS;

	x = VID_TO_PIXEL(ctx->xmark_frac);
	y = ctx->ymark;
	index = ctx->cli_index;

	/* rounded up character position in this line */
	xpos = (x + ctx->x_charsize - 1) / ctx->x_charsize;

	/* number of characters which can fit on this (first) line */
	xspace = ctx->cols - xpos;

	if (!curs->indent && index > xspace) {
		/* move to the next line */
		y += ctx->y_charsize;
		index -= xspace;

		/* figure out the available space in subsequent lines */
		if (!curs->indent) {
			xspace = ctx->cols;
			x = 0;
		}

		/* calculate the line based on that */
		y += index / xspace;
		x += (index % xspace) * fontdata->width;
	} else {
		x += index * fontdata->width;
	}

	/* place the cursor 1 pixel before the start of the next char */
	if (x > 0)
		x -= 1;

	/* Store line pointer and height in cursor struct */
	curs->x = x;
	curs->y = y;
	curs->height = ctx->y_charsize;
	curs->index = ctx->cli_index;

	return 0;
}

static int console_putc_xy(struct udevice *dev, void *vctx, uint x_frac,
			   uint y, int cp)
{
	return console_normal_putc_xy(dev, vctx, x_frac, y, cp);
}

static const struct vidconsole_ops console_ops = {
	.putc_xy	= console_putc_xy,
	.move_rows	= console_move_rows,
	.set_row	= console_set_row,
	.get_font_size	= console_simple_get_font_size,
	.get_font	= console_simple_get_font,
	.select_font	= console_simple_select_font,
	.ctx_new	= console_simple_ctx_new,
#ifdef CONFIG_CURSOR
	.get_cursor_info	= console_get_cursor_info,
#endif
};

U_BOOT_DRIVER(vidconsole_normal) = {
	.name		= "vidconsole0",
	.id		= UCLASS_VIDEO_CONSOLE,
	.ops		= &console_ops,
	.priv_auto	= sizeof(struct console_simple_priv),
};
