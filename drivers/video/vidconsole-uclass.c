// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 * (C) Copyright 2001-2015
 * DENX Software Engineering -- wd@denx.de
 * Compulab Ltd - http://compulab.co.il/
 * Bernecker & Rainer Industrieelektronik GmbH - http://www.br-automation.com
 */

#define LOG_CATEGORY UCLASS_VIDEO_CONSOLE

#include <abuf.h>
#include <charset.h>
#include <command.h>
#include <console.h>
#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <spl.h>
#include <video.h>
#include <video_console.h>
#include "vidconsole_internal.h"
#include <video_font.h>		/* Bitmap font for code page 437 */
#include <linux/ctype.h>

int vidconsole_putc_xy(struct udevice *dev, void *vctx, uint x, uint y, int ch)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->putc_xy)
		return -ENOSYS;
	return ops->putc_xy(dev, vctx ?: vidconsole_ctx_from_priv(priv), x, y,
			    ch);
}

int vidconsole_move_rows(struct udevice *dev, uint rowdst, uint rowsrc,
			 uint count)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->move_rows)
		return -ENOSYS;
	return ops->move_rows(dev, rowdst, rowsrc, count);
}

int vidconsole_set_row(struct udevice *dev, uint row, int clr)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->set_row)
		return -ENOSYS;
	return ops->set_row(dev, row, clr);
}

int vidconsole_entry_start(struct udevice *dev, void *ctx)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ctx)
		ctx = vidconsole_ctx_from_priv(dev_get_uclass_priv(dev));
	if (!ops->entry_start)
		return -ENOSYS;
	return ops->entry_start(dev, ctx);
}

/* Move backwards one space, ctx must be non-NULL */
static int vidconsole_back(struct udevice *dev, struct vidconsole_ctx *ctx)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	int ret;

	if (ops->backspace) {
		ret = ops->backspace(dev, ctx);
		if (ret != -ENOSYS)
			return ret;
	}

	/* Hide cursor at old position if it's visible */
	vidconsole_hide_cursor(dev, ctx);

	ctx->xcur_frac -= VID_TO_POS(ctx->x_charsize);
	if (ctx->xcur_frac < ctx->xstart_frac) {
		ctx->xcur_frac = (ctx->cols - 1) *
			VID_TO_POS(ctx->x_charsize);
		ctx->ycur -= ctx->y_charsize;
		if (ctx->ycur < 0)
			ctx->ycur = 0;
	}
	assert(ctx->cli_index);
	cli_index_adjust(ctx, -1);

	return video_sync(dev->parent, false);
}

/*
 * Move to a newline, scrolling the display if necessary.
 * ctx must be non-NULL
 */
static void vidconsole_newline(struct udevice *dev, struct vidconsole_ctx *ctx)
{
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);
	const int rows = CONFIG_VAL(CONSOLE_SCROLL_LINES);
	int i, ret;

	ctx->xcur_frac = ctx->xstart_frac;
	ctx->ycur += ctx->y_charsize;

	/* Check if we need to scroll the terminal */
	if (vid_priv->rot % 2 ?
	    ctx->ycur + ctx->x_charsize > vid_priv->xsize :
	    ctx->ycur + ctx->y_charsize > vid_priv->ysize) {
		vidconsole_move_rows(dev, 0, rows, ctx->rows - rows);
		for (i = 0; i < rows; i++)
			vidconsole_set_row(dev, ctx->rows - i - 1,
					   vid_priv->colour_bg);
		ctx->ycur -= rows * ctx->y_charsize;
	}
	ctx->last_ch = 0;

	ret = video_sync(dev->parent, false);
	if (ret) {
#ifdef DEBUG
		err_puts(true, "[vc err: video_sync]");
#endif
	}
}

static char *parsenum(char *s, int *num)
{
	char *end;
	*num = simple_strtol(s, &end, 10);
	return end;
}

void vidconsole_set_cursor_pos(struct udevice *dev, void *vctx, int x, int y)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx_from_priv(priv);

	/* Hide cursor at old position if it's visible */
	vidconsole_hide_cursor(dev, ctx);

	ctx->xcur_frac = VID_TO_POS(x);
	ctx->xstart_frac = ctx->xcur_frac;
	ctx->ycur = y;

	/* make sure not to kern against the previous character */
	ctx->last_ch = 0;
	vidconsole_entry_start(dev, ctx);
}

/**
 * set_cursor_position() - set cursor position
 *
 * @priv:	private data of the video console
 * @row:	new row
 * @col:	new column
 */
static void set_cursor_position(struct udevice *dev, int row, int col)
{
	struct vidconsole_ctx *ctx = vidconsole_ctx(dev);

	/*
	 * Ensure we stay in the bounds of the screen.
	 */
	if (row >= ctx->rows)
		row = ctx->rows - 1;
	if (col >= ctx->cols)
		col = ctx->cols - 1;

	vidconsole_position_cursor(dev, col, row);
}

/**
 * get_cursor_position() - get cursor position
 *
 * @priv:	private data of the video console
 * @row:	row
 * @col:	column
 */
static void get_cursor_position(struct vidconsole_priv *priv,
				int *row, int *col)
{
	struct vidconsole_ctx *ctx = vidconsole_ctx_from_priv(priv);

	*row = ctx->ycur / ctx->y_charsize;
	*col = VID_TO_PIXEL(ctx->xcur_frac - ctx->xstart_frac) /
	       ctx->x_charsize;
}

/*
 * Process a character while accumulating an escape string.  Chars are
 * accumulated into escape_buf until the end of escape sequence is
 * found, at which point the sequence is parsed and processed.
 */
static void vidconsole_escape_char(struct udevice *dev,
				   struct vidconsole_ctx *ctx, char ch)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ansi *ansi = &ctx->ansi;

	if (!IS_ENABLED(CONFIG_VIDEO_ANSI))
		goto error;

	/* Sanity checking for bogus ESC sequences: */
	if (ansi->escape_len >= sizeof(ansi->escape_buf))
		goto error;
	if (ansi->escape_len == 0) {
		switch (ch) {
		case '7':
			/* Save cursor position */
			get_cursor_position(priv, &ansi->row_saved,
					    &ansi->col_saved);
			ansi->escape = 0;

			return;
		case '8': {
			/* Restore cursor position */
			int row = ansi->row_saved;
			int col = ansi->col_saved;

			set_cursor_position(dev, row, col);
			ansi->escape = 0;
			return;
		}
		case '[':
			break;
		default:
			goto error;
		}
	}

	ansi->escape_buf[ansi->escape_len++] = ch;

	/*
	 * Escape sequences are terminated by a letter, so keep
	 * accumulating until we get one:
	 */
	if (!isalpha(ch))
		return;

	/*
	 * clear escape mode first, otherwise things will get highly
	 * surprising if you hit any debug prints that come back to
	 * this console.
	 */
	ansi->escape = 0;

	switch (ch) {
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F': {
		int row, col, num;
		char *s = ansi->escape_buf;

		/*
		 * Cursor up/down: [%dA, [%dB, [%dE, [%dF
		 * Cursor left/right: [%dD, [%dC
		 */
		s++;    /* [ */
		s = parsenum(s, &num);
		if (num == 0)			/* No digit in sequence ... */
			num = 1;		/* ... means "move by 1". */

		get_cursor_position(priv, &row, &col);
		if (ch == 'A' || ch == 'F')
			row -= num;
		if (ch == 'C')
			col += num;
		if (ch == 'D')
			col -= num;
		if (ch == 'B' || ch == 'E')
			row += num;
		if (ch == 'E' || ch == 'F')
			col = 0;
		if (col < 0)
			col = 0;
		if (row < 0)
			row = 0;
		/* Right and bottom overflows are handled in the callee. */
		set_cursor_position(dev, row, col);
		break;
	}
	case 'H':
	case 'f': {
		int row, col;
		char *s = ansi->escape_buf;

		/*
		 * Set cursor position: [%d;%df or [%d;%dH
		 */
		s++;    /* [ */
		s = parsenum(s, &row);
		s++;    /* ; */
		s = parsenum(s, &col);

		/*
		 * Video origin is [0, 0], terminal origin is [1, 1].
		 */
		if (row)
			--row;
		if (col)
			--col;

		set_cursor_position(dev, row, col);

		break;
	}
	case 'J': {
		int mode;

		/*
		 * Clear part/all screen:
		 *   [J or [0J - clear screen from cursor down
		 *   [1J       - clear screen from cursor up
		 *   [2J       - clear entire screen
		 *
		 * TODO we really only handle entire-screen case, others
		 * probably require some additions to video-uclass (and
		 * are not really needed yet by efi_console)
		 */
		parsenum(ansi->escape_buf + 1, &mode);

		if (mode == 2) {
			int ret;

			video_clear(dev->parent);
			ret = video_sync(dev->parent, false);
			if (ret) {
#ifdef DEBUG
				err_puts(true, "[vc err: video_sync]");
#endif
			}
			ctx->ycur = 0;
			ctx->xcur_frac = ctx->xstart_frac;
		} else {
			debug("unsupported clear mode: %d\n", mode);
		}
		break;
	}
	case 'K': {
		struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
		int mode;

		/*
		 * Clear (parts of) current line
		 *   [0K       - clear line to end
		 *   [2K       - clear entire line
		 */
		parsenum(ansi->escape_buf + 1, &mode);

		if (mode == 2) {
			int row, col;

			get_cursor_position(priv, &row, &col);
			vidconsole_set_row(dev, row, vid_priv->colour_bg);
		}
		break;
	}
	case 'm': {
		struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
		char *s = ansi->escape_buf;
		char *end = &ansi->escape_buf[ansi->escape_len];

		/*
		 * Set graphics mode: [%d;...;%dm
		 *
		 * Currently only supports the color attributes:
		 *
		 * Foreground Colors:
		 *
		 *   30	Black
		 *   31	Red
		 *   32	Green
		 *   33	Yellow
		 *   34	Blue
		 *   35	Magenta
		 *   36	Cyan
		 *   37	White
		 *
		 * Background Colors:
		 *
		 *   40	Black
		 *   41	Red
		 *   42	Green
		 *   43	Yellow
		 *   44	Blue
		 *   45	Magenta
		 *   46	Cyan
		 *   47	White
		 */

		s++;    /* [ */
		while (s < end) {
			int val;

			s = parsenum(s, &val);
			s++;

			switch (val) {
			case 0:
				/* all attributes off */
				video_set_default_colors(dev->parent, false);
				break;
			case 1:
				/* bold */
				vid_priv->fg_col_idx |= 8;
				vid_priv->colour_fg = video_index_to_colour(
						vid_priv, vid_priv->fg_col_idx);
				break;
			case 7:
				/* reverse video */
				vid_priv->colour_fg = video_index_to_colour(
						vid_priv, vid_priv->bg_col_idx);
				vid_priv->colour_bg = video_index_to_colour(
						vid_priv, vid_priv->fg_col_idx);
				break;
			case 30 ... 37:
				/* foreground color */
				vid_priv->fg_col_idx &= ~7;
				vid_priv->fg_col_idx |= val - 30;
				vid_priv->colour_fg = video_index_to_colour(
						vid_priv, vid_priv->fg_col_idx);
				break;
			case 40 ... 47:
				/* background color, also mask the bold bit */
				vid_priv->bg_col_idx &= ~0xf;
				vid_priv->bg_col_idx |= val - 40;
				vid_priv->colour_bg = video_index_to_colour(
						vid_priv, vid_priv->bg_col_idx);
				break;
			default:
				/* ignore unsupported SGR parameter */
				break;
			}
		}

		break;
	}
	default:
		debug("unrecognized escape sequence: %*s\n",
		      ansi->escape_len, ansi->escape_buf);
	}

	return;

error:
	/* something went wrong, just revert to normal mode: */
	ansi->escape = 0;
}

/* Put that actual character on the screen (using the UTF-32 code points). */
static int vidconsole_output_glyph(struct udevice *dev,
				   struct vidconsole_ctx *ctx, int ch)
{
	int ret;

	if (_DEBUG)
		err_printf(true, "glyph last_ch '%c': ch '%c' (%02x): ",
			   ctx->last_ch, ch >= ' ' ? ch : ' ', ch);

	/*
	 * Failure of this function normally indicates an unsupported
	 * colour depth. Check this and return an error to help with
	 * diagnosis.
	 */
	ret = vidconsole_putc_xy(dev, ctx, ctx->xcur_frac, ctx->ycur, ch);
	if (ret == -EAGAIN) {
		vidconsole_newline(dev, ctx);
		ret = vidconsole_putc_xy(dev, ctx, ctx->xcur_frac, ctx->ycur,
					 ch);
	}
	if (ret < 0)
		return ret;
	ctx->xcur_frac += ret;
	ctx->last_ch = ch;
	if (ctx->xcur_frac >= ctx->xsize_frac)
		vidconsole_newline(dev, ctx);
	cli_index_adjust(ctx, 1);

	return 0;
}

int vidconsole_put_char(struct udevice *dev, void *vctx, char ch)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx_from_priv(priv);
	struct vidconsole_ansi *ansi = &ctx->ansi;
	int cp, ret;

	/* Hide cursor to avoid artifacts */
	vidconsole_hide_cursor(dev, ctx);

	if (ansi->escape) {
		vidconsole_escape_char(dev, ctx, ch);
		return 0;
	}

	switch (ch) {
	case '\x1b':
		ansi->escape_len = 0;
		ansi->escape = 1;
		break;
	case '\a':
		/* beep */
		break;
	case '\r':
		ctx->xcur_frac = ctx->xstart_frac;
		break;
	case '\n':
		vidconsole_newline(dev, ctx);
		vidconsole_entry_start(dev, ctx);
		break;
	case '\t':	/* Tab (8 chars alignment) */
		ctx->xcur_frac = ((ctx->xcur_frac / ctx->tab_width_frac)
				+ 1) * ctx->tab_width_frac;

		if (ctx->xcur_frac >= ctx->xsize_frac)
			vidconsole_newline(dev, ctx);
		break;
	case '\b':
		vidconsole_back(dev, ctx);
		ctx->last_ch = 0;
		break;
	default:
		if (IS_ENABLED(CONFIG_CHARSET)) {
			cp = utf8_to_utf32_stream(ch, ctx->utf8_buf);
			if (cp == 0)
				return 0;
		} else {
			cp = ch;
		}
		ret = vidconsole_output_glyph(dev, ctx, cp);
		if (ret < 0)
			return ret;
		break;
	}

	return 0;
}

int vidconsole_put_stringn(struct udevice *dev, void *ctx, const char *str,
			   int maxlen)
{
	const char *s, *end = NULL;
	int ret;

	if (maxlen != -1)
		end = str + maxlen;
	for (s = str; *s && (maxlen == -1 || s < end); s++) {
		ret = vidconsole_put_char(dev, ctx, *s);
		if (ret)
			return ret;
	}

	return 0;
}

int vidconsole_put_string(struct udevice *dev, void *ctx, const char *str)
{
	return vidconsole_put_stringn(dev, ctx, str, -1);
}

static void vidconsole_putc(struct stdio_dev *sdev, const char ch)
{
	struct udevice *dev = sdev->priv;
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	int ret;

	if (priv->quiet)
		return;
	ret = vidconsole_put_char(dev, NULL, ch);
	if (ret) {
#ifdef DEBUG
		err_puts(true, "[vc err: putc]");
#endif
	}
	ret = video_sync(dev->parent, false);
	if (ret) {
#ifdef DEBUG
		err_puts(true, "[vc err: video_sync]");
#endif
	}
}

static void vidconsole_putsn(struct stdio_dev *sdev, const char *s, int len)
{
	struct udevice *dev = sdev->priv;
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	int ret;

	if (priv->quiet)
		return;
	ret = vidconsole_put_stringn(dev, NULL, s, len);
	if (ret) {
#ifdef DEBUG
		char str[30];

		snprintf(str, sizeof(str), "[vc err: putsn %d]", ret);
		err_puts(true, str);
#endif
	}
	ret = video_sync(dev->parent, false);
	if (ret) {
#ifdef DEBUG
		err_puts(true, "[vc err: video_sync]");
#endif
	}
}

static void vidconsole_puts(struct stdio_dev *sdev, const char *s)
{
	vidconsole_putsn(sdev, s, strlen(s));
}

void vidconsole_list_fonts(struct udevice *dev)
{
	struct vidfont_info info;
	int ret, i;

	for (i = 0, ret = 0; !ret; i++) {
		ret = vidconsole_get_font(dev, i, &info);
		if (!ret)
			printf("%s\n", info.name);
	}
}

int vidconsole_get_font(struct udevice *dev, int seq,
			struct vidfont_info *info)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ops->get_font)
		return -ENOSYS;

	return ops->get_font(dev, seq, info);
}

int vidconsole_get_font_size(struct udevice *dev, void *ctx, const char **name,
			     uint *sizep)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ctx)
		ctx = vidconsole_ctx(dev);
	if (!ops->get_font_size)
		return -ENOSYS;

	*name = ops->get_font_size(dev, ctx, sizep);
	return 0;
}

int vidconsole_select_font(struct udevice *dev, void *ctx, const char *name,
			   uint size)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (!ctx)
		ctx = vidconsole_ctx(dev);
	if (!ops->select_font)
		return -ENOSYS;

	return ops->select_font(dev, ctx, name, size);
}

int vidconsole_measure(struct udevice *dev, const char *name, uint size,
		       const char *text, int len, int limit,
		       struct vidconsole_bbox *bbox, struct alist *lines)
{
	struct vidconsole_ctx *ctx = vidconsole_ctx(dev);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	int ret;

	if (ops->measure) {
		if (lines)
			alist_empty(lines);
		ret = ops->measure(dev, name, size, text, len, limit, bbox,
				   lines);
		if (ret != -ENOSYS)
			return ret;
	}

	bbox->valid = true;
	bbox->x0 = 0;
	bbox->y0 = 0;
	bbox->x1 = ctx->x_charsize * (len < 0 ? strlen(text) : len);
	bbox->y1 = ctx->y_charsize;

	return 0;
}

int vidconsole_nominal(struct udevice *dev, const char *name, uint size,
		       uint num_chars, struct vidconsole_bbox *bbox)
{
	struct vidconsole_ctx *ctx = vidconsole_ctx(dev);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	int ret;

	if (ops->nominal) {
		ret = ops->nominal(dev, name, size, num_chars, bbox);
		if (ret != -ENOSYS)
			return ret;
	}

	bbox->valid = true;
	bbox->x0 = 0;
	bbox->y0 = 0;
	bbox->x1 = ctx->x_charsize * num_chars;
	bbox->y1 = ctx->y_charsize;

	return 0;
}

int vidconsole_ctx_new(struct udevice *dev, void **ctxp)
{
	struct udevice *vid = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid);
	struct vidconsole_uc_plat *plat = dev_get_uclass_plat(dev);
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	struct vidconsole_ctx *ctx;
	int ret = -ENOMEM, size;
	void **ptr;

	if (!ops->ctx_new)
		return -ENOSYS;

	/* reserve space first so ctx_new failure doesn't need cleanup */
	ptr = alist_add_placeholder(&priv->ctx_list);
	if (!ptr)
		return -ENOMEM;

	size = plat->ctx_size ?: sizeof(struct vidconsole_ctx);
	ctx = calloc(1, size);
	if (!ctx)
		goto err_alloc;
	*ptr = ctx;

	if (CONFIG_IS_ENABLED(CONFIG_CURSOR) && xpl_phase() == PHASE_BOARD_R) {
		ret = console_alloc_cursor(dev, &ctx->curs);
		if (ret)
			goto err_curs;
	}

	ctx->xsize_frac = VID_TO_POS(vid_priv->xsize);

	ret = ops->ctx_new(dev, ctx);
	if (ret)
		goto err_new;
	*ctxp = ctx;

	return 0;

err_new:
	console_free_cursor(&ctx->curs);
err_curs:
err_alloc:
	priv->ctx_list.count--;
	free(ctx);

	return ret;
}

/**
 * vidconsole_free_ctx() - Free a context without removing it from the list
 *
 * This calls the driver's ctx_dispose method and frees the save_data, but
 * does not remove the context from the alist.
 *
 * @dev: Vidconsole device
 * @ctx: Context to free
 */
static void vidconsole_free_ctx(struct udevice *dev, struct vidconsole_ctx *ctx)
{
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	if (ops->ctx_dispose)
		ops->ctx_dispose(dev, ctx);
	console_free_cursor(&ctx->curs);
	free(ctx);
}

int vidconsole_ctx_dispose(struct udevice *dev, void *vctx)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx;
	void **ptr, **from;

	if (!ctx)
		return 0;

	/* remove the context from the list */
	alist_for_each_filter(ptr, from, &priv->ctx_list) {
		if (*ptr != ctx)
			*from++ = *ptr;
	}
	alist_update_end(&priv->ctx_list, from);

	vidconsole_free_ctx(dev, ctx);

	return 0;
}

#ifdef CONFIG_CURSOR
int vidconsole_show_cursor(struct udevice *dev, void *vctx)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx_from_priv(priv);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);
	struct vidconsole_cursor *curs = &ctx->curs;
	int ret;

	/* find out where the cursor should be drawn */
	if (!ops->get_cursor_info)
		return -ENOSYS;

	ret = ops->get_cursor_info(dev, ctx);
	if (ret)
		return ret;

	/* If the driver stored cursor line and height, use them for drawing */
	if (curs->height) {
		struct udevice *vid = dev_get_parent(dev);
		struct video_priv *vid_priv = dev_get_uclass_priv(vid);

		/*
		 * avoid drawing off the display - we assume that the driver
		 * ensures that curs->y < vid_priv->ysize
		 */
		curs->height = min(curs->height, vid_priv->ysize - curs->y);

		ret = cursor_show(curs, vid_priv, NORMAL_DIRECTION);
		if (ret)
			return ret;

		/* Update display damage for cursor area */
		video_damage(vid, curs->x, curs->y, VIDCONSOLE_CURSOR_WIDTH,
			     curs->height);
	}

	curs->visible = true;

	return 0;
}

int vidconsole_hide_cursor(struct udevice *dev, void *vctx)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx_from_priv(priv);
	struct vidconsole_cursor *curs = &ctx->curs;
	int ret;

	if (!curs->visible)
		return 0;

	/* If the driver stored cursor line and height, use them for drawing */
	if (curs->height) {
		struct udevice *vid = dev_get_parent(dev);
		struct video_priv *vid_priv = dev_get_uclass_priv(vid);

		ret = cursor_hide(curs, vid_priv, NORMAL_DIRECTION);
		if (ret)
			return ret;

		/* Update display damage for cursor area */
		video_damage(vid, curs->x, curs->y, VIDCONSOLE_CURSOR_WIDTH,
			     curs->height);
	}

	curs->visible = false;

	return 0;
}
#endif /* CONFIG_CURSOR */

int vidconsole_mark_start(struct udevice *dev, void *vctx)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx_from_priv(priv);
	struct vidconsole_ops *ops = vidconsole_get_ops(dev);

	ctx->xmark_frac = ctx->xcur_frac;
	ctx->ymark = ctx->ycur;
	ctx->cli_index = 0;
	if (ops->mark_start) {
		int ret;

		ret = ops->mark_start(dev, ctx);
		if (ret != -ENOSYS)
			return ret;
	}

	return 0;
}

void vidconsole_push_colour(struct udevice *dev, enum colour_idx fg,
			    enum colour_idx bg, struct vidconsole_colour *old)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);

	old->colour_fg = vid_priv->colour_fg;
	old->colour_bg = vid_priv->colour_bg;

	vid_priv->colour_fg = video_index_to_colour(vid_priv, fg);
	vid_priv->colour_bg = video_index_to_colour(vid_priv, bg);
}

void vidconsole_pop_colour(struct udevice *dev, struct vidconsole_colour *old)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);

	vid_priv->colour_fg = old->colour_fg;
	vid_priv->colour_bg = old->colour_bg;
}

/* Set up the number of rows and colours (rotated drivers override this) */
static int vidconsole_pre_probe(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);

	alist_init_struct(&priv->ctx_list, void *);

	return 0;
}

/* Register the device with stdio */
static int vidconsole_post_probe(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct stdio_dev *sdev = &priv->sdev;
	struct vidconsole_ctx *ctx;
	int ret;

	ret = vidconsole_ctx_new(dev, (void **)&ctx);
	if (ret)
		return ret;
	priv->ctx = ctx;

	if (!ctx->tab_width_frac)
		ctx->tab_width_frac = VID_TO_POS(ctx->x_charsize) * 8;

	if (dev_seq(dev)) {
		snprintf(sdev->name, sizeof(sdev->name), "vidconsole%d",
			 dev_seq(dev));
	} else {
		strcpy(sdev->name, "vidconsole");
	}

	sdev->flags = DEV_FLAGS_OUTPUT | DEV_FLAGS_DM;
	sdev->putc = vidconsole_putc;
	sdev->puts = vidconsole_puts;
	sdev->putsn = vidconsole_putsn;
	sdev->priv = dev;

	return stdio_register(sdev);
}

static int vidconsole_pre_remove(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	void **ptr;

	/* free all contexts in the list, including the default ctx */
	alist_for_each(ptr, &priv->ctx_list)
		vidconsole_free_ctx(dev, *ptr);
	alist_uninit(&priv->ctx_list);
	priv->ctx = NULL;

	return 0;
}

UCLASS_DRIVER(vidconsole) = {
	.id		= UCLASS_VIDEO_CONSOLE,
	.name		= "vidconsole0",
	.pre_probe	= vidconsole_pre_probe,
	.post_probe	= vidconsole_post_probe,
	.pre_remove	= vidconsole_pre_remove,
	.per_device_auto	= sizeof(struct vidconsole_priv),
	.per_device_plat_auto	= sizeof(struct vidconsole_uc_plat),
};

int vidconsole_clear_and_reset(struct udevice *dev)
{
	int ret;

	ret = video_clear(dev_get_parent(dev));
	if (ret)
		return ret;
	vidconsole_position_cursor(dev, 0, 0);

	return 0;
}

void vidconsole_position_cursor(struct udevice *dev, unsigned col, unsigned row)
{
	struct vidconsole_ctx *ctx = vidconsole_ctx(dev);
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);
	short x, y;

	x = min_t(short, col * ctx->x_charsize, vid_priv->xsize - 1);
	y = min_t(short, row * ctx->y_charsize, vid_priv->ysize - 1);
	vidconsole_set_cursor_pos(dev, NULL, x, y);
}

void vidconsole_set_quiet(struct udevice *dev, bool quiet)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);

	priv->quiet = quiet;
}

void vidconsole_set_bitmap_font(struct udevice *dev, struct vidconsole_ctx *ctx,
				struct video_fontdata *fontdata)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);

	log_debug("console_simple: setting %s font\n", fontdata->name);
	log_debug("width: %d\n", fontdata->width);
	log_debug("byte width: %d\n", fontdata->byte_width);
	log_debug("height: %d\n", fontdata->height);

	ctx->x_charsize = fontdata->width;
	ctx->y_charsize = fontdata->height;
	if (vid_priv->rot % 2) {
		ctx->cols = vid_priv->ysize / fontdata->width;
		ctx->rows = vid_priv->xsize / fontdata->height;
		ctx->xsize_frac = VID_TO_POS(vid_priv->ysize);
	} else {
		ctx->cols = vid_priv->xsize / fontdata->width;
		ctx->rows = vid_priv->ysize / fontdata->height;
		ctx->xsize_frac = VID_TO_POS(vid_priv->xsize);
	}
	ctx->xstart_frac = 0;
}

static void vidconsole_idle_ctx(struct udevice *dev, struct vidconsole_ctx *ctx)
{
	struct vidconsole_cursor *curs = &ctx->curs;

	/* Only handle cursor if it's enabled */
	if (curs->enabled && !curs->visible) {
		/*
		 * TODO(sjg@chromium.org): We are using a saved position here,
		 * but vidconsole_show_cursor() calls get_cursor_info() to
		 * recalc the position anyway.
		 */
		vidconsole_show_cursor(dev, ctx);
	}
}

void vidconsole_idle(struct udevice *dev)
{
	struct vidconsole_priv *priv = dev_get_uclass_priv(dev);
	struct vidconsole_ctx **ctxp;

	alist_for_each(ctxp, &priv->ctx_list)
		vidconsole_idle_ctx(dev, *ctxp);
}

#ifdef CONFIG_CURSOR
void vidconsole_readline_start(struct udevice *dev, void *vctx, bool indent)
{
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx(dev);

	ctx->curs.indent = indent;
	ctx->curs.enabled = true;
	vidconsole_mark_start(dev, ctx);
}

void vidconsole_readline_end(struct udevice *dev, void *vctx)
{
	struct vidconsole_ctx *ctx = vctx ?: vidconsole_ctx(dev);

	ctx->curs.enabled = false;
}

void vidconsole_readline_start_all(bool indent)
{
	struct uclass *uc;
	struct udevice *dev;

	uclass_id_foreach_dev(UCLASS_VIDEO_CONSOLE, dev, uc)
		vidconsole_readline_start(dev, NULL, indent);
}

void vidconsole_readline_end_all(void)
{
	struct uclass *uc;
	struct udevice *dev;

	uclass_id_foreach_dev(UCLASS_VIDEO_CONSOLE, dev, uc)
		vidconsole_readline_end(dev, NULL);
}
#endif /* CURSOR */

void *vidconsole_ctx(struct udevice *dev)
{
	struct vidconsole_priv *uc_priv = dev_get_uclass_priv(dev);

	return uc_priv->ctx;
}
