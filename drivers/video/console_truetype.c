// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Google, Inc
 */

#define LOG_CATEGORY	UCLASS_VIDEO

#include <abuf.h>
#include <console.h>
#include <dm.h>
#include <log.h>
#include <malloc.h>
#include <spl.h>
#include <video.h>
#include <video_console.h>
#include <video_font.h>
#include "vidconsole_internal.h"

/* Functions needed by stb_truetype.h */
static int tt_floor(double val)
{
	if (val < 0)
		return (int)(val - 0.999);

	return (int)val;
}

static int tt_ceil(double val)
{
	if (val < 0)
		return (int)val;

	return (int)(val + 0.999);
}

static double frac(double val)
{
	return val - tt_floor(val);
}

static double tt_fabs(double x)
{
	return x < 0 ? -x : x;
}

 /*
  * Simple square root algorithm. This is from:
  * http://stackoverflow.com/questions/1623375/writing-your-own-square-root-function
  * Written by Chihung Yu
  * Creative Commons license
  * http://creativecommons.org/licenses/by-sa/3.0/legalcode
  * It has been modified to compile correctly, and for U-Boot style.
  */
static double tt_sqrt(double value)
{
	double lo = 1.0;
	double hi = value;

	while (hi - lo > 0.00001) {
		double mid = lo + (hi - lo) / 2;

		if (mid * mid - value > 0.00001)
			hi = mid;
		else
			lo = mid;
	}

	return lo;
}

static double tt_fmod(double x, double y)
{
	double rem;

	if (y == 0.0)
		return 0.0;
	rem = x - (x / y) * y;

	return rem;
}

/* dummy implementation */
static double tt_pow(double x, double y)
{
	return 0;
}

/* dummy implementation */
static double tt_cos(double val)
{
	return 0;
}

/* dummy implementation */
static double tt_acos(double val)
{
	return 0;
}

#define STBTT_ifloor		tt_floor
#define STBTT_iceil		tt_ceil
#define STBTT_fabs		tt_fabs
#define STBTT_sqrt		tt_sqrt
#define STBTT_pow		tt_pow
#define STBTT_fmod		tt_fmod
#define STBTT_cos		tt_cos
#define STBTT_acos		tt_acos

/* Scratch buffer for zero-malloc rendering - must match stb_truetype.h */
#define STBTT_SCRATCH_DEFINED
struct stbtt_scratch {
	char *buf;
	size_t size;
	size_t used;
};

static inline void stbtt_scratch_reset(struct stbtt_scratch *s)
{
	if (s)
		s->used = 0;
}

static inline void *stbtt__scratch_alloc(size_t size, void *userdata)
{
	struct stbtt_scratch *s = userdata;
	size_t aligned = (size + 7) & ~7;

	if (s && s->used + aligned <= s->size) {
		void *p = s->buf + s->used;

		s->used += aligned;

		return p;
	}

	return malloc(size);
}

static inline void stbtt__scratch_free(void *ptr, void *userdata)
{
	struct stbtt_scratch *s = userdata;

	if (!s || ptr < (void *)s->buf || ptr >= (void *)(s->buf + s->size))
		free(ptr);
}

#define STBTT_malloc(size, u)	stbtt__scratch_alloc(size, u)
#define STBTT_free(ptr, u)	stbtt__scratch_free(ptr, u)
#define STBTT_assert(x)
#define STBTT_strlen(x)		strlen(x)
#define STBTT_memcpy		memcpy
#define STBTT_memset		memset

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/**
 * struct pos_info - Records a cursor position
 *
 * @xpos_frac:	Fractional X position in pixels (multiplied by VID_FRAC_DIV)
 * @ypos:	Y position (pixels from the top)
 * @width:	Width of the character at this position in pixels (rounded up)
 * @cp:		Unicode code point of the character
 */
struct pos_info {
	int xpos_frac;
	int ypos;
	int width;
	int cp;
};

/*
 * Allow one for each character on the command line plus one for each newline.
 * This is just an estimate, but it should not be exceeded.
 */
#define POS_HISTORY_SIZE	(CONFIG_SYS_CBSIZE * 11 / 10)

/**
 * struct console_tt_metrics - Information about a font / size combination
 *
 * This caches various font metrics which are expensive to regenerate each time
 * the font size changes. There is one of these for each font / size combination
 * that is being used
 *
 * @font_name:	Name of the font
 * @font_size:	Vertical font size in pixels
 * @font_data:	Pointer to TrueType font file contents
 * @font:	TrueType font information for the current font
 * @baseline:	Pixel offset of the font's baseline from the cursor position.
 *		This is the 'ascent' of the font, scaled to pixel coordinates.
 *		It measures the distance from the baseline to the top of the
 *		font.
 * @scale:	Scale of the font. This is calculated from the pixel height
 *		of the font. It is used by the STB library to generate images
 *		of the correct size.
 */
struct console_tt_metrics {
	const char *font_name;
	int font_size;
	const u8 *font_data;
	stbtt_fontinfo font;
	int baseline;
	double scale;
};

/**
 * struct console_tt_ctx - Per-client context for this driver
 *
 * @com:	Common fields from the vidconsole uclass
 * @cur_met:	Current metrics being used
 * @pos_ptr:	Current position in the position history
 * @pos_start:	Value of pos_ptr when the cursor is at the start of the text
 *		being entered by the user
 * @pos_count:	Maximum value reached by pos_ptr (initially zero)
 * @pos:	List of cursor positions for each character written. This is
 *		used to handle backspace. We clear the frame buffer between
 *		the last position and the current position, thus erasing the
 *		last character. We record enough characters to go back to the
 *		start of the current command line.
 */
struct console_tt_ctx {
	struct vidconsole_ctx com;
	struct console_tt_metrics *cur_met;
	struct video_fontdata *cur_fontdata;
	int pos_ptr;
	int pos_start;
	int pos_count;
	struct pos_info pos[POS_HISTORY_SIZE];
};

/**
 * struct console_tt_priv - Private data for this driver
 *
 * @metrics:	List metrics that can be used
 * @num_metrics:	Number of available metrics
 * @glyph_buf:	Pre-allocated buffer for rendering glyphs. If a glyph fits,
 *	this avoids malloc/free per character. Allocated lazily after
 *	relocation to avoid using early malloc space.
 * @glyph_buf_size: Current size of glyph_buf in bytes
 * @scratch: Scratch buffer state for stbtt internal allocations
 * @scratch_buf: Memory for scratch buffer
 */
struct console_tt_priv {
	struct console_tt_metrics metrics[CONFIG_CONSOLE_TRUETYPE_MAX_METRICS];
	int num_metrics;
	u8 *glyph_buf;
	int glyph_buf_size;
	struct stbtt_scratch scratch;
	char *scratch_buf;
};

static int console_truetype_set_row(struct udevice *dev, uint row, int clr)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_tt_ctx *ctx = vidconsole_ctx(dev);
	struct vidconsole_ctx *com = &ctx->com;
	void *end, *line;
	int font_height;

	/* Get font height from current font type */
	if (ctx->cur_fontdata)
		font_height = ctx->cur_fontdata->height;
	else
		font_height = ctx->cur_met->font_size;

	line = vid_priv->fb + row * font_height * vid_priv->line_length;
	end = line + font_height * vid_priv->line_length;

	switch (vid_priv->bpix) {
	case VIDEO_BPP8: {
		u8 *dst;

		if (IS_ENABLED(CONFIG_VIDEO_BPP8)) {
			for (dst = line; dst < (u8 *)end; ++dst)
				*dst = clr;
		}
		break;
	}
	case VIDEO_BPP16: {
		u16 *dst = line;

		if (IS_ENABLED(CONFIG_VIDEO_BPP16)) {
			for (dst = line; dst < (u16 *)end; ++dst)
				*dst = clr;
		}
		break;
	}
	case VIDEO_BPP32: {
		u32 *dst = line;

		if (IS_ENABLED(CONFIG_VIDEO_BPP32)) {
			for (dst = line; dst < (u32 *)end; ++dst)
				*dst = clr;
		}
		break;
	}
	default:
		return -ENOSYS;
	}

	video_damage(dev->parent,
		     0,
		     com->y_charsize * row,
		     vid_priv->xsize,
		     com->y_charsize);

	return 0;
}

static int console_truetype_move_rows(struct udevice *dev, uint rowdst,
				     uint rowsrc, uint count)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_tt_ctx *ctx = vidconsole_ctx(dev);
	struct vidconsole_ctx *com = &ctx->com;
	void *dst;
	void *src;
	int i, diff, font_height;

	/* Get font height from current font type */
	if (ctx->cur_fontdata)
		font_height = ctx->cur_fontdata->height;
	else
		font_height = ctx->cur_met->font_size;

	dst = vid_priv->fb + rowdst * font_height * vid_priv->line_length;
	src = vid_priv->fb + rowsrc * font_height * vid_priv->line_length;
	memmove(dst, src, font_height * vid_priv->line_length * count);

	/* Scroll up our position history */
	diff = (rowsrc - rowdst) * font_height;
	for (i = 0; i < ctx->pos_ptr; i++)
		ctx->pos[i].ypos -= diff;

	video_damage(dev->parent,
		     0,
		     com->y_charsize * rowdst,
		     vid_priv->xsize,
		     com->y_charsize * count);

	return 0;
}

/**
 * clear_from() - Clear characters on the display from given index onwards
 *
 * Erases all characters from the specified position index in the position
 * history to the end of the position array (pos_count). This handles line
 * wrapping by clearing to the end of lines and continuing on subsequent lines.
 *
 * @dev:	Device to update
 * @index:	Starting index in priv->pos array to erase from
 */
static void clear_from(struct udevice *dev, int index)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_tt_ctx *ctx = vidconsole_ctx(dev);
	struct vidconsole_ctx *com = &ctx->com;
	struct udevice *vid_dev = dev->parent;
	struct pos_info *start_pos, *end_pos;
	int xstart, xend;
	int ystart, yend;

	assert(ctx->pos_count && index && index < ctx->pos_count);

	start_pos = &ctx->pos[index];
	xstart = VID_TO_PIXEL(start_pos->xpos_frac);
	ystart = start_pos->ypos;

	/* End position is the last character in the position array */
	end_pos = &ctx->pos[ctx->pos_count - 1];
	xend = VID_TO_PIXEL(end_pos->xpos_frac) + end_pos->width;
	yend = end_pos->ypos;

	/* If on the same line, just erase from start to end position */
	if (ystart == yend) {
		video_fill_part(vid_dev, xstart, ystart, xend,
				ystart + com->y_charsize,
				vid_priv->colour_bg);
	} else {
		/* Different lines - erase to end of first line */
		video_fill_part(vid_dev, xstart, ystart, vid_priv->xsize,
				ystart + com->y_charsize, vid_priv->colour_bg);

		/* Erase any complete lines in between */
		if (yend > ystart + com->y_charsize) {
			video_fill_part(vid_dev, 0, ystart + com->y_charsize,
					vid_priv->xsize, yend, vid_priv->colour_bg);
		}

		/* Erase from start of final line to end of last character */
		video_fill_part(vid_dev, 0, yend, xend,
				yend + com->y_charsize,
				vid_priv->colour_bg);
	}
}

static int console_truetype_putc_xy(struct udevice *dev, void *vctx, uint x,
				    uint y, int cp)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_tt_ctx *ctx = vctx;
	struct vidconsole_ctx *com = &ctx->com;
	struct console_tt_priv *priv = dev_get_priv(dev);
	struct console_tt_metrics *met = ctx->cur_met;
	stbtt_fontinfo *font;
	int width, height, xoff, yoff;
	double xpos, x_shift;
	int lsb;
	int width_frac, linenum;
	struct pos_info *pos;
	u8 *bits, *data;
	int advance;
	void *start, *end, *line;
	int row, kern;
	bool use_buf;

	/* Use fixed font if selected */
	if (ctx->cur_fontdata)
		return console_fixed_putc_xy(dev, &ctx->com, x, y, cp,
					     ctx->cur_fontdata);

	/* Reset scratch buffer for this character */
	stbtt_scratch_reset(&priv->scratch);

	/* First get some basic metrics about this character */
	font = &met->font;
	stbtt_GetCodepointHMetrics(font, cp, &advance, &lsb);

	/*
	 * First out our current X position in fractional pixels. If we wrote
	 * a character previously, use kerning to fine-tune the position of
	 * this character */
	pos = ctx->pos_ptr < ctx->pos_count ? &ctx->pos[ctx->pos_ptr] : NULL;
	xpos = frac(VID_TO_PIXEL((double)x));
	kern = 0;
	if (com->last_ch) {
		int last_cp = com->last_ch;

		if (pos)
			last_cp = pos->cp;
		kern = stbtt_GetCodepointKernAdvance(font, last_cp, cp);
		if (_DEBUG) {
			err_printf(true, "kern %c (%02x)", last_cp, last_cp);
		}
		xpos += met->scale * kern;
	}
	if (_DEBUG) {
		err_printf(true, " %c (%02x)\n", cp >= ' ' ? cp : ' ', cp);
	}

	/*
	 * Figure out where the cursor will move to after this character, and
	 * abort if we are out of space on this line. Also calculate the
	 * effective width of this character, which will be our return value:
	 * it dictates how much the cursor will move forward on the line.
	 */
	x_shift = xpos - (double)tt_floor(xpos);
	xpos += advance * met->scale;
	width_frac = (int)VID_TO_POS((kern + advance) * met->scale);
	if (x + width_frac >= com->xsize_frac)
		return -EAGAIN;

	/* Write the current cursor position into history */
	if (ctx->pos_ptr < POS_HISTORY_SIZE) {
		bool erase = false;

		/* Check if we're overwriting a different character */
		if (pos && pos->cp != cp) {
			erase = true;
			/* Erase using the old character's position before updating */
			clear_from(dev, ctx->pos_ptr);

			/* After erasing, we don't care about erased characters */
			ctx->pos_count = ctx->pos_ptr;
		}

		pos = &ctx->pos[ctx->pos_ptr];
		pos->xpos_frac = com->xcur_frac;
		pos->ypos = com->ycur;
		pos->width = (width_frac + VID_FRAC_DIV - 1) / VID_FRAC_DIV;
		pos->cp = cp;
		ctx->pos_ptr++;
		if (ctx->pos_ptr > ctx->pos_count)
			ctx->pos_count = ctx->pos_ptr;
	}

	/*
	 * Figure out how much past the start of a pixel we are, and pass this
	 * information into the render, which will return a 8-bit-per-pixel
	 * image of the character. For empty characters, like ' ', data will
	 * return NULL;
	 *
	 * Use the pre-allocated glyph buffer if large enough, falling back to
	 * malloc for oversized glyphs. This avoids alloc/free traffic for
	 * normal characters.
	 */
	{
		int ix0, iy0, ix1, iy1;

		stbtt_GetCodepointBitmapBoxSubpixel(font, cp, met->scale,
						    met->scale, x_shift, 0,
						    &ix0, &iy0, &ix1, &iy1);
		width = ix1 - ix0;
		height = iy1 - iy0;
		xoff = ix0;
		yoff = iy0;
	}
	if (!width || !height)
		return width_frac;

	/*
	 * Use the pre-allocated buffer if available and large enough. Allocate
	 * it lazily, but only after relocation to avoid using early malloc.
	 * Use realloc() to grow the buffer as needed.
	 */
	use_buf = false;
	if (IS_ENABLED(CONFIG_CONSOLE_TRUETYPE_GLYPH_BUF) &&
	    xpl_phase() >= PHASE_BOARD_R) {
		int need_size = width * height;

		if (need_size > priv->glyph_buf_size) {
			int new_size = SZ_4K;

			/* use the next power of 2 */
			while (new_size < need_size)
				new_size <<= 1;
			priv->glyph_buf = realloc(priv->glyph_buf, new_size);
			if (priv->glyph_buf)
				priv->glyph_buf_size = new_size;
		}
		if (priv->glyph_buf) {
			data = priv->glyph_buf;
			use_buf = true;
		}
	}
	if (!use_buf) {
		data = malloc(width * height);
		if (!data)
			return width_frac;
	}
	gd_inc_glyph_count();

	stbtt_MakeCodepointBitmapSubpixel(font, data, width, height, width,
					  met->scale, met->scale, x_shift, 0,
					  cp);

	/* Figure out where to write the character in the frame buffer */
	bits = data;
	start = vid_priv->fb + y * vid_priv->line_length +
		VID_TO_PIXEL(x) * VNBYTES(vid_priv->bpix);
	linenum = met->baseline + yoff;
	if (linenum > 0)
		start += linenum * vid_priv->line_length;
	line = start;

	/*
	 * Write a row at a time, converting the 8bpp image into the colour
	 * depth of the display. We only expect white-on-black or the reverse
	 * so the code only handles this simple case.
	 */
	for (row = 0; row < height; row++) {
		switch (vid_priv->bpix) {
		case VIDEO_BPP8:
			if (IS_ENABLED(CONFIG_VIDEO_BPP8)) {
				u8 *dst = line + xoff;
				int i;

				for (i = 0; i < width; i++) {
					int val = *bits;
					int out;

					if (vid_priv->colour_bg)
						val = 255 - val;
					out = val;
					if (vid_priv->colour_fg)
						*dst++ |= out;
					else
						*dst++ &= out;
					bits++;
				}
				end = dst;
			}
			break;
		case VIDEO_BPP16: {
			uint16_t *dst = (uint16_t *)line + xoff;
			int i;

			if (IS_ENABLED(CONFIG_VIDEO_BPP16)) {
				for (i = 0; i < width; i++) {
					int val = *bits;
					int out;

					if (vid_priv->colour_bg)
						val = 255 - val;
					out = val >> 3 |
						(val >> 2) << 5 |
						(val >> 3) << 11;
					if (vid_priv->colour_fg)
						*dst++ |= out;
					else
						*dst++ &= out;
					bits++;
				}
				end = dst;
			}
			break;
		}
		case VIDEO_BPP32: {
			u32 *dst = (u32 *)line + xoff;
			int i;

			if (IS_ENABLED(CONFIG_VIDEO_BPP32)) {
				for (i = 0; i < width; i++) {
					int val = *bits;
					int out;

					if (vid_priv->colour_bg)
						val = 255 - val;
					if (vid_priv->format == VIDEO_X2R10G10B10)
						out = val << 2 | val << 12 | val << 22;
					else
						out = val | val << 8 | val << 16;
					if (vid_priv->colour_fg)
						*dst++ |= out;
					else
						*dst++ &= out;
					bits++;
				}
				end = dst;
			}
			break;
		}
		default:
			if (!use_buf)
				free(data);
			return -ENOSYS;
		}

		line += vid_priv->line_length;
	}

	video_damage(dev->parent,
		     VID_TO_PIXEL(x) + xoff,
		     y + met->baseline + yoff,
		     width,
		     height);

	if (!use_buf)
		free(data);

	return width_frac;
}

/**
 * console_truetype_backspace() - Handle a backspace operation
 *
 * This clears the previous character so that the console looks as if it had
 * not been entered.
 *
 * @dev:	Device to update
 * @vctx:	Vidconsole context to use
 * Return: 0 if OK, -ENOSYS if not supported
 */
static int console_truetype_backspace(struct udevice *dev, void *vctx)
{
	struct video_priv *vid_priv = dev_get_uclass_priv(dev->parent);
	struct console_tt_ctx *ctx = vctx;
	struct vidconsole_ctx *com = &ctx->com;
	struct pos_info *pos;
	int xend;

	/*
	 * This indicates a very strange error higher in the stack. The caller
	 * has sent out n character and n + 1 backspaces.
	 */
	if (!ctx->pos_ptr)
		return -ENOSYS;

	/* Pop the last cursor position off the stack */
	pos = &ctx->pos[--ctx->pos_ptr];

	/*
	 * Figure out the end position for clearing. Normally it is the current
	 * cursor position, but if we are clearing a character on the previous
	 * line, we clear from the end of the line.
	 */
	if (pos->ypos == com->ycur)
		xend = VID_TO_PIXEL(com->xcur_frac);
	else
		xend = vid_priv->xsize;

	/* Move the cursor back to where it was when we pushed this record */
	com->xcur_frac = pos->xpos_frac;
	com->ycur = pos->ypos;

	return 0;
}

static int console_truetype_entry_start(struct udevice *dev, void *vctx)
{
	struct console_tt_ctx *ctx = vctx;
	struct vidconsole_ctx *com = &ctx->com;

	/* A new input line has start, so clear our history */
	ctx->pos_ptr = 0;
	ctx->pos_count = 0;
	com->last_ch = 0;

	return 0;
}

/*
 * Provides a list of fonts which can be obtained at run-time in U-Boot. These
 * are compiled in by the Makefile.
 *
 * At present there is no mechanism to select a particular font - the first
 * one found is the one that is used. But the build system and the code here
 * supports multiple fonts, which may be useful for certain firmware screens.
 */
struct font_info {
	char *name;
	u8 *begin;
	u8 *end;
};

#define FONT_DECL(_name) \
	extern u8 __ttf_ ## _name ## _begin[]; \
	extern u8 __ttf_ ## _name ## _end[];

#define TT_FONT_ENTRY(_name)		{ \
	.name = #_name, \
	.begin = __ttf_ ## _name ## _begin, \
	.end = __ttf_ ## _name ## _end, \
	}

FONT_DECL(nimbus_sans_l_regular);
FONT_DECL(ankacoder_c75_r);
FONT_DECL(rufscript010);
FONT_DECL(cantoraone_regular);
FONT_DECL(ubuntu_light);
FONT_DECL(ubuntu_bold);
FONT_DECL(dejavu_mono);

static struct font_info font_table[] = {
#ifdef CONFIG_CONSOLE_TRUETYPE_NIMBUS
	TT_FONT_ENTRY(nimbus_sans_l_regular),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_ANKACODER
	TT_FONT_ENTRY(ankacoder_c75_r),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_RUFSCRIPT
	TT_FONT_ENTRY(rufscript010),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_CANTORAONE
	TT_FONT_ENTRY(cantoraone_regular),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_UBUNTU_LIGHT
	TT_FONT_ENTRY(ubuntu_light),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_UBUNTU_BOLD
	TT_FONT_ENTRY(ubuntu_bold),
#endif
#ifdef CONFIG_CONSOLE_TRUETYPE_DEJAVU
	TT_FONT_ENTRY(dejavu_mono),
#endif
	{} /* sentinel */
};

/**
 * font_valid() - Check if a font-table entry is valid
 *
 * Depending on available files in the build system, fonts may end up being
 * empty.
 *
 * @return true if the entry is valid
 */
static inline bool font_valid(struct font_info *tab)
{
	return abs(tab->begin - tab->end) > 4;
}

/**
 * console_truetype_find_font() - Find a suitable font
 *
 * This searches for the first available font.
 *
 * Return: pointer to the font-table entry, or NULL if none is found
 */
static struct font_info *console_truetype_find_font(void)
{
	struct font_info *tab;

	for (tab = font_table; tab->begin; tab++) {
		if (font_valid(tab)) {
			debug("%s: Font '%s', at %p, size %lx\n", __func__,
			      tab->name, tab->begin,
			      (ulong)(tab->end - tab->begin));
			return tab;
		}
	}

	return NULL;
}

int console_truetype_get_font(struct udevice *dev, int seq,
			      struct vidfont_info *info)
{
	struct font_info *tab;
	struct video_fontdata *fontdata;
	int i;

	/* List fixed fonts first */
	for (i = 0, fontdata = fonts; fontdata->name; fontdata++, i++) {
		if (i == seq) {
			info->name = fontdata->name;
			return 0;
		}
	}

	/* then list TrueType fonts */
	for (tab = font_table; tab->begin; tab++, i++) {
		if (i == seq && font_valid(tab)) {
			info->name = tab->name;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * truetype_add_metrics() - Add a new font/size combination
 *
 * @dev:	Video console device to update
 * @font_name:	Name of font
 * @font_size:	Size of the font (norminal pixel height)
 * @font_data:	Pointer to the font data
 * @return 0 if OK, -EPERM if stbtt failed, -E2BIG if the the metrics table is
 *	full
 */
static int truetype_add_metrics(struct udevice *dev, const char *font_name,
				uint font_size, const void *font_data)
{
	struct console_tt_priv *priv = dev_get_priv(dev);
	struct console_tt_metrics *met;
	stbtt_fontinfo *font;
	int ascent;

	if (priv->num_metrics == CONFIG_CONSOLE_TRUETYPE_MAX_METRICS)
		return log_msg_ret("num", -E2BIG);

	met = &priv->metrics[priv->num_metrics];
	met->font_name = font_name;
	met->font_size = font_size;
	met->font_data = font_data;

	font = &met->font;
	if (!stbtt_InitFont(font, font_data, 0)) {
		debug("%s: Font init failed\n", __func__);
		return -EPERM;
	}
	font->userdata = &priv->scratch;

	/* Pre-calculate some things we will need regularly */
	met->scale = stbtt_ScaleForPixelHeight(font, font_size);
	stbtt_GetFontVMetrics(font, &ascent, 0, 0);
	met->baseline = (int)(ascent * met->scale);

	return priv->num_metrics++;
}

/**
 * find_metrics() - Find the metrics for a given font and size
 *
 * @dev:	Video console device to update
 * @name:	Name of font
 * @size:	Size of the font (norminal pixel height)
 * @return metrics, if found, else NULL
 */
static struct console_tt_metrics *find_metrics(struct udevice *dev,
					       const char *name, uint size)
{
	struct console_tt_priv *priv = dev_get_priv(dev);
	int i;

	for (i = 0; i < priv->num_metrics; i++) {
		struct console_tt_metrics *met = &priv->metrics[i];

		if (!strcmp(name, met->font_name) && met->font_size == size)
			return met;
	}

	return NULL;
}

/**
 * set_bitmap_font() - Set up console to use a fixed font
 *
 * @dev:	Console device
 * @ctx:	Console context
 * @fontdata:	Fixed font data to use
 * Return: 0 if OK, -ve on error
 */
static void set_bitmap_font(struct udevice *dev, struct console_tt_ctx *ctx,
			    struct video_fontdata *fontdata)
{
	struct vidconsole_ctx *com = &ctx->com;

	ctx->cur_fontdata = fontdata;
	ctx->cur_met = NULL;

	vidconsole_set_bitmap_font(dev, com, fontdata);

	com->tab_width_frac = VID_TO_POS(fontdata->width) * 8 / 2;
}

static void select_metrics(struct udevice *dev, struct console_tt_ctx *ctx,
			   struct console_tt_metrics *met)
{
	struct vidconsole_ctx *com = &ctx->com;
	struct udevice *vid_dev = dev_get_parent(dev);
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);

	ctx->cur_met = met;
	com->x_charsize = met->font_size;
	com->y_charsize = met->font_size;
	com->xstart_frac = VID_TO_POS(2);
	com->cols = vid_priv->xsize / met->font_size;
	com->rows = vid_priv->ysize / met->font_size;
	com->tab_width_frac = VID_TO_POS(met->font_size) * 8 / 2;
}

static int get_metrics(struct udevice *dev, const char *name, uint size,
		       struct console_tt_metrics **metp)
{
	struct console_tt_priv *priv = dev_get_priv(dev);
	struct console_tt_metrics *met;
	struct font_info *tab;

	if (name || size) {
		if (!size)
			size = CONFIG_CONSOLE_TRUETYPE_SIZE;
		if (!name)
			name = font_table->name;

		met = find_metrics(dev, name, size);
		if (!met) {
			for (tab = font_table; tab->begin; tab++) {
				if (font_valid(tab) &&
				    !strcmp(name, tab->name)) {
					int ret;

					ret = truetype_add_metrics(dev,
								   tab->name,
								   size,
								   tab->begin);
					if (ret < 0)
						return log_msg_ret("add", ret);

					met = &priv->metrics[ret];
					break;
				}
			}
		}
		if (!met)
			return log_msg_ret("find", -ENOENT);
	} else {
		/* Use the default font */
		met = priv->metrics;
	}

	*metp = met;

	return 0;
}

static int truetype_select_font(struct udevice *dev, void *vctx,
				const char *name, uint size)
{
	struct console_tt_ctx *ctx = vctx;
	struct console_tt_metrics *met;
	struct video_fontdata *fontdata;
	int ret;

	/* Check if this is a request for a fixed font */
	if (name) {
		for (fontdata = fonts; fontdata->name; fontdata++) {
			if (!strcmp(name, fontdata->name)) {
				/* Switch to fixed-font mode */
				set_bitmap_font(dev, ctx, fontdata);
				return 0;
			}
		}
	}

	/* Continue with TrueType font selection */
	ctx->cur_fontdata = NULL;
	ret = get_metrics(dev, name, size, &met);
	if (ret)
		return log_msg_ret("sel", ret);

	select_metrics(dev, ctx, met);

	return 0;
}

static int truetype_measure(struct udevice *dev, const char *name, uint size,
			    const char *text, int len, int pixel_limit,
			    struct vidconsole_bbox *bbox, struct alist *lines)
{
	struct console_tt_metrics *met;
	struct vidconsole_mline mline;
	const char *s, *last_space, *end;
	int width, last_width;
	stbtt_fontinfo *font;
	int lsb, advance;
	int start;
	int limit;
	int lastch;
	int ret;

	ret = get_metrics(dev, name, size, &met);
	if (ret)
		return log_msg_ret("sel", ret);

	bbox->valid = false;
	if (!*text) {
		bbox->x0 = 0;
		bbox->y0 = 0;
		bbox->x1 = 0;
		bbox->y1 = 0;
		return 0;
	}

	limit = -1;
	if (pixel_limit != -1)
		limit = tt_ceil((double)pixel_limit / met->scale);

	end = len < 0 ? NULL : text + len;
	font = &met->font;
	width = 0;
	bbox->y1 = 0;
	bbox->x1 = 0;
	start = 0;
	last_space = NULL;
	last_width = 0;
	for (lastch = 0, s = text; *s && s != end; s++) {
		int neww;
		int ch = *s;

		if (ch == ' ') {
			/*
			 * store the position and width so we can use it again
			 * if we need to word-wrap
			 */
			last_space = s;
			last_width = width;
		}

		/* First get some basic metrics about this character */
		stbtt_GetCodepointHMetrics(font, ch, &advance, &lsb);
		neww = width + advance;

		/* Use kerning to fine-tune the position of this character */
		if (lastch)
			neww += stbtt_GetCodepointKernAdvance(font, lastch, ch);
		lastch = ch;

		/* see if we need to start a new line */
		if (ch == '\n' || (limit != -1 && neww >= limit)) {
			if (ch != '\n' && last_space) {
				s = last_space;
				width = last_width;
			}
			last_space = NULL;
			mline.bbox.x0 = 0;
			mline.bbox.y0 = bbox->y1;
			mline.bbox.x1 = tt_ceil((double)width * met->scale);
			mline.xpos = (int)((double)width * met->scale);
			bbox->x1 = max(bbox->x1, mline.bbox.x1);
			bbox->y1 += met->font_size;
			mline.bbox.y1 = bbox->y1;
			mline.bbox.valid = true;
			mline.start = start;
			mline.len = (s - text) - start;
			if (lines && !alist_add(lines, mline))
				return log_msg_ret("ttm", -ENOMEM);
			log_content("line x1 %d y0 %d y1 %d start %d len %d text '%.*s'\n",
				    mline.bbox.x1, mline.bbox.y0, mline.bbox.y1,
				    mline.start, mline.len, mline.len,
				    text + mline.start);

			start = s - text;
			start++;
			lastch = 0;
			neww = 0;
		}

		width = neww;
	}

	/* add the final line */
	mline.bbox.x0 = 0;
	mline.bbox.y0 = bbox->y1;
	mline.bbox.x1 = tt_ceil((double)width * met->scale);
	mline.xpos = (int)((double)width * met->scale);
	bbox->y1 += met->font_size;
	mline.bbox.y1 = bbox->y1;
	mline.start = start;
	mline.len = (s - text) - start;
	if (lines && !alist_add(lines, mline))
		return log_msg_ret("ttM", -ENOMEM);
	log_content("line x1 %d y0 %d y1 %d start %d len %d text '%.*s'\n",
		    mline.bbox.x1, mline.bbox.y0, mline.bbox.y1,
		    mline.start, mline.len, mline.len, text + mline.start);

	bbox->valid = true;
	bbox->x0 = 0;
	bbox->y0 = 0;
	bbox->x1 = max(bbox->x1, mline.bbox.x1);

	return 0;
}

static int truetype_nominal(struct udevice *dev, const char *name, uint size,
			    uint num_chars, struct vidconsole_bbox *bbox)
{
	struct console_tt_metrics *met;
	stbtt_fontinfo *font;
	int lsb, advance;
	int width;
	int ret;

	ret = get_metrics(dev, name, size, &met);
	if (ret)
		return log_msg_ret("sel", ret);

	font = &met->font;
	width = 0;

	/* First get some basic metrics about this character */
	stbtt_GetCodepointHMetrics(font, 'W', &advance, &lsb);

	width = advance;

	bbox->valid = true;
	bbox->x0 = 0;
	bbox->y0 = 0;
	bbox->x1 = tt_ceil((double)width * num_chars * met->scale);
	bbox->y1 = met->font_size;

	return 0;
}

static int truetype_ctx_new(struct udevice *dev, void *vctx)
{
	struct console_tt_priv *priv = dev_get_priv(dev);
	struct console_tt_ctx *ctx = vctx;

	/* use the first set of metrics by default */
	ctx->cur_met = &priv->metrics[0];

	select_metrics(dev, ctx, ctx->cur_met);

	return 0;
}

static int truetype_get_cursor_info(struct udevice *dev, void *vctx)
{
	struct console_tt_ctx *ctx = vctx;
	struct vidconsole_ctx *com = &ctx->com;
	struct vidconsole_cursor *curs = &com->curs;
	int x, y, index;
	uint height;

	if (xpl_phase() <= PHASE_SPL)
		return -ENOSYS;

	/*
	 * Figure out where to place the cursor.
	 *
	 * A current quirk is that the cursor is always at xcur_frac, since we
	 * output characters directly to the console as they are typed by the
	 * user. So we never bother with ctx->pos[index] for now.
	 */
	index = ctx->pos_ptr;
	if (0 && index < ctx->pos_count)
		x = VID_TO_PIXEL(ctx->pos[index].xpos_frac);
	else
		x = VID_TO_PIXEL(com->xcur_frac);
	y = com->ycur;

	/* Get font height from current font type */
	if (ctx->cur_fontdata)
		height = ctx->cur_fontdata->height;
	else
		height = ctx->cur_met->font_size;

	/* Store line pointer and height in cursor struct */
	curs->x = x;
	curs->y = y;
	curs->height = height;
	curs->index = index;

	return 0;
}

const char *console_truetype_get_font_size(struct udevice *dev, void *vctx,
					   uint *sizep)
{
	struct console_tt_ctx *ctx = vctx;

	if (ctx->cur_fontdata) {
		/* Using fixed font */
		*sizep = ctx->cur_fontdata->height;
		return ctx->cur_fontdata->name;
	} else {
		/* Using TrueType font */
		struct console_tt_metrics *met = ctx->cur_met;

		*sizep = met->font_size;
		return met->font_name;
	}
}

static int truetype_mark_start(struct udevice *dev, void *vctx)
{
	struct console_tt_ctx *ctx = vctx;

	ctx->pos_start = ctx->pos_ptr;

	return 0;
}

static int console_truetype_probe(struct udevice *dev)
{
	struct console_tt_priv *priv = dev_get_priv(dev);
	struct udevice *vid_dev = dev->parent;
	struct video_priv *vid_priv = dev_get_uclass_priv(vid_dev);
	struct font_info *tab;
	uint font_size;
	int ret;

	debug("%s: start\n", __func__);

	/* Allocate scratch buffer for stbtt internal allocations */
	if (CONFIG_IS_ENABLED(CONSOLE_TRUETYPE_SCRATCH)) {
		priv->scratch_buf = malloc(CONFIG_CONSOLE_TRUETYPE_SCRATCH_SIZE);
		if (priv->scratch_buf) {
			priv->scratch.buf = priv->scratch_buf;
			priv->scratch.size = CONFIG_CONSOLE_TRUETYPE_SCRATCH_SIZE;
			priv->scratch.used = 0;
		}
	}

	if (vid_priv->font_size)
		font_size = vid_priv->font_size;
	else
		font_size = CONFIG_CONSOLE_TRUETYPE_SIZE;
	tab = console_truetype_find_font();
	if (!tab) {
		debug("%s: Could not find any fonts\n", __func__);
		return -EBFONT;
	}

	ret = truetype_add_metrics(dev, tab->name, font_size, tab->begin);
	if (ret < 0)
		return log_msg_ret("add", ret);

	debug("%s: ready\n", __func__);

	return 0;
}

static int console_truetype_remove(struct udevice *dev)
{
	struct console_tt_priv *priv = dev_get_priv(dev);

	free(priv->scratch_buf);
	free(priv->glyph_buf);

	return 0;
}

static const struct vidconsole_ops console_truetype_ops = {
	.putc_xy	= console_truetype_putc_xy,
	.move_rows	= console_truetype_move_rows,
	.set_row	= console_truetype_set_row,
	.backspace	= console_truetype_backspace,
	.entry_start	= console_truetype_entry_start,
	.get_font	= console_truetype_get_font,
	.get_font_size	= console_truetype_get_font_size,
	.select_font	= truetype_select_font,
	.measure	= truetype_measure,
	.nominal	= truetype_nominal,
	.ctx_new	= truetype_ctx_new,
	.get_cursor_info	= truetype_get_cursor_info,
	.mark_start	= truetype_mark_start,
};

static int console_truetype_bind(struct udevice *dev)
{
	struct vidconsole_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->ctx_size = sizeof(struct console_tt_ctx);

	return 0;
}

U_BOOT_DRIVER(vidconsole_truetype) = {
	.name	= "vidconsole_tt",
	.id	= UCLASS_VIDEO_CONSOLE,
	.ops	= &console_truetype_ops,
	.bind	= console_truetype_bind,
	.probe	= console_truetype_probe,
	.remove	= console_truetype_remove,
	.priv_auto	= sizeof(struct console_tt_priv),
};
