/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2015 Google, Inc
 */

#ifndef __video_console_h
#define __video_console_h

#include <alist.h>
#include <video.h>

struct abuf;
struct video_fontdata;
struct video_priv;

#define VID_FRAC_DIV	256

#define VID_TO_PIXEL(x)	((x) / VID_FRAC_DIV)
#define VID_TO_POS(x)	((x) * VID_FRAC_DIV)

enum {
	/* cursor width in pixels */
	VIDCONSOLE_CURSOR_WIDTH		= 2,
};

/**
 * struct vidconsole_cursor - cursor state for a video console
 *
 * The cursor is set up and maintained by the vidconsole. It is a simple
 * vertical bar of width VIDCONSOLE_CURSOR_WIDTH shown in the foreground colour.
 *
 * No cursor processing is done unless @enabled is true.
 *
 * To figure out where to draw the cursor, the vidconsole's get_cursor_info() is
 * called. It fills in the @x, @y, @height and @index information below. That
 * information remains valid while the cursor is shown, so it can be used to
 * hide the cursor.
 *
 * The cursor is drawn only when idle (by vidconsole_show_cursor() called from
 * vidconsole_idle()). It is erased as soon as any output needs to be written to
 * the display - vidconsole_hide_cursor() is called from a few places in the
 * vidconsole uclass.
 *
 * Under the hood, vidconsole_show_cursor() calls cursor_show() which saves the
 * pixels under the cursor as it draws it. Once finished it sets @visible to
 * true, so that we know we must erase the cursor before drawing anything else.
 *
 * The old pixels end up in @save_data and are used by cursor_hide() (called
 * from vidconsole_hide_cursor()) to restore the display contents to what they
 * were. Once that is done, @visible is set back to false.
 *
 * @enabled:	cursor is active (e.g. during readline)
 * @visible:	cursor is currently visible
 * @indent:	indent subsequent lines to the same position as the first line
 * @saved:	true if save_data contains valid data
 * @save_data:	saved pixels under cursor
 * @x:		cursor left X position in pixels
 * @y:		cursor top Y position in pixels
 * @height:	height of cursor in pixels
 * @index:	cursor index within the CLI or field being edited
 */
struct vidconsole_cursor {
	bool enabled;
	bool visible;
	bool indent;
	bool saved;
	u32 *save_data;

	/* filled in by get_cursor_info(): */
	uint x;
	uint y;
	uint height;
	uint index;
};

/**
 * struct vidconsole_ansi - ANSI escape-sequence state
 *
 * ANSI escape sequences are accumulated character by character, starting after
 * the ESC char (0x1b) until the entire sequence is consumed, at which point it
 * is acted upon.
 *
 * @escape:	True if currently accumulating an ANSI escape sequence
 * @escape_len:	Length of accumulated escape sequence so far
 * @row_saved:	Saved Y position in pixels (0=top)
 * @col_saved:	Saved X position, in fractional units (VID_TO_POS(x))
 * @escape_buf:	Buffer to accumulate escape sequence
 */
struct vidconsole_ansi {
	int escape;
	int escape_len;
	int row_saved;
	int col_saved;
	char escape_buf[32];
};

/**
 * struct vidconsole_ctx - per-client context for a video console
 *
 * This holds per-client state for video consoles. It can be used by clients
 * to maintain separate contexts for different text-entry operations.
 *
 * @rows:		Number of text rows
 * @cols:		Number of text columns
 * @x_charsize:		Character width in pixels
 * @y_charsize:		Character height in pixels
 * @xcur_frac:		Current X position, in fractional units (VID_TO_POS(x))
 * @ycur:		Current Y position in pixels (0=top)
 * @last_ch:		Last character written to the text console on this line
 * @cli_index:		Character index into the CLI text (0=start)
 * @xmark_frac:		X position of start of CLI text entry, in fractional units
 * @ymark:		Y position of start of CLI text
 * @ansi:		ANSI escape-sequence state
 * @utf8_buf:		Buffer to accumulate UTF-8 byte sequence
 * @curs:		Cursor state and management
 * @xstart_frac:	Left margin for the text console in fractional units
 * @tab_width_frac:	Tab width in fractional units
 * @xsize_frac:		Width of the display in fractional units
 */
struct vidconsole_ctx {
	int rows;
	int cols;
	int x_charsize;
	int y_charsize;
	int xcur_frac;
	int ycur;
	int last_ch;
	int cli_index;
	int xmark_frac;
	int ymark;
	struct vidconsole_ansi ansi;
	char utf8_buf[5];
	struct vidconsole_cursor curs;
	int xstart_frac;
	int tab_width_frac;
	int xsize_frac;
};

/**
 * struct vidconsole_uc_plat - uclass platform data for a vidconsole device
 *
 * This holds information that the uclass needs to know about each device. It
 * is accessed using dev_get_uclass_plat(dev).
 *
 * @ctx_size: Size of context data needed by the driver, or 0 to use the
 *	default (sizeof(struct vidconsole_ctx))
 */
struct vidconsole_uc_plat {
	uint ctx_size;
};

/**
 * struct vidconsole_priv - uclass-private data about a console device
 *
 * Drivers must set up @ctx->rows, @ctx->cols, @ctx->x_charsize,
 * @ctx->y_charsize in their probe() method. Drivers may set up
 * @ctx->xstart_frac if desired.
 *
 * Note that these values relate to the rotated console, so that an 80x25
 * console which is rotated 90 degrees will have rows=80 and cols=25
 *
 * The ctx->xcur_frac and ctx->ycur values refer to the unrotated coordinates,
 * that is ctx->xcur_frac always advances with each character, even if its
 * limit might be vid_priv->ysize instead of vid_priv->xsize if the console is
 * rotated 90 or 270 degrees.
 *
 * @sdev:		stdio device, acting as an output sink
 * @ctx:		Per-client context (allocated by the uclass)
 * @ctx_list:		List of additional contexts allocated by clients
 * @quiet:		Suppress all output from stdio
 */
struct vidconsole_priv {
	struct stdio_dev sdev;
	struct vidconsole_ctx *ctx;
	struct alist ctx_list;
	bool quiet;
};

/**
 * struct vidfont_info - information about a font
 *
 * @name: Font name, e.g. nimbus_sans_l_regular
 */
struct vidfont_info {
	const char *name;
};

/**
 * struct vidconsole_colour - Holds colour information
 *
 * @colour_fg:	Foreground colour (pixel value)
 * @colour_bg:	Background colour (pixel value)
 */
struct vidconsole_colour {
	u32 colour_fg;
	u32 colour_bg;
};

/**
 * struct vidconsole_bbox - Bounding box of text
 *
 * This describes the bounding box of something, measured in pixels. The x0/y0
 * pair is inclusive; the x1/y2 pair is exclusive, meaning that it is one pixel
 * beyond the extent of the object
 *
 * @valid: Values are valid (bounding box is known)
 * @x0: left x position, in pixels from left side
 * @y0: top y position, in pixels from top
 * @x1: right x position + 1
 * @y1: botton y position + 1
 */
struct vidconsole_bbox {
	bool valid;
	int x0;
	int y0;
	int x1;
	int y1;
};

/**
 * vidconsole_mline - Holds information about a line of measured text
 *
 * @bbox: Bounding box of the line, assuming it starts at 0,0
 * @xpos: Cursor x position at end of line (truncated, not ceiled like bbox.x1)
 * @start: String index of the first character in the line
 * @len: Number of characters in the line
 */
struct vidconsole_mline {
	struct vidconsole_bbox bbox;
	int xpos;
	int start;
	int len;
};

/**
 * struct vidconsole_ops - Video console operations
 *
 * These operations work on either an absolute console position (measured
 * in pixels) or a text row number (measured in rows, where each row consists
 * of an entire line of text - typically 16 pixels).
 */
struct vidconsole_ops {
	/**
	 * putc_xy() - write a single character to a position
	 *
	 * @dev:	Device to write to
	 * @ctx:	Vidconsole context to use (cannot be NULL)
	 * @x_frac:	Fractional pixel X position (0=left-most pixel) which
	 *		is the X position multipled by VID_FRAC_DIV.
	 * @y:		Pixel Y position (0=top-most pixel)
	 * @cp:		UTF-32 code point to write
	 * @return number of fractional pixels that the cursor should move,
	 * if all is OK, -EAGAIN if we ran out of space on this line, other -ve
	 * on error
	 */
	int (*putc_xy)(struct udevice *dev, void *ctx, uint x_frac, uint y,
		       int cp);

	/**
	 * move_rows() - Move text rows from one place to another
	 *
	 * @dev:	Device to adjust
	 * @rowdst:	Destination text row (0=top)
	 * @rowsrc:	Source start text row
	 * @count:	Number of text rows to move
	 * @return 0 if OK, -ve on error
	 */
	int (*move_rows)(struct udevice *dev, uint rowdst, uint rowsrc,
			  uint count);

	/**
	 * set_row() - Set the colour of a text row
	 *
	 * Every pixel contained within the text row is adjusted
	 *
	 * @dev:	Device to adjust
	 * @row:	Text row to adjust (0=top)
	 * @clr:	Raw colour (pixel value) to write to each pixel
	 * @return 0 if OK, -ve on error
	 */
	int (*set_row)(struct udevice *dev, uint row, int clr);

	/**
	 * entry_start() - Indicate that text entry is starting afresh
	 *
	 * @dev:	Device to adjust
	 * @ctx:	Vidconsole context to use (cannot be NULL)
	 * Returns: 0 on success, -ve on error
	 *
	 * Consoles which use proportional fonts need to track the position of
	 * each character output so that backspace will return to the correct
	 * place. This method signals to the console driver that a new entry
	 * line is being start (e.g. the user pressed return to start a new
	 * command). The driver can use this signal to empty its list of
	 * positions.
	 */
	int (*entry_start)(struct udevice *dev, void *ctx);

	/**
	 * backspace() - Handle erasing the last character
	 *
	 * @dev:	Device to adjust
	 * @ctx:	Vidconsole context to use
	 * Returns: 0 on success, -ve on error
	 *
	 * With proportional fonts the vidconsole uclass cannot itself erase
	 * the previous character. This optional method will be called when
	 * a backspace is needed. The driver should erase the previous
	 * character and update the cursor position (xcur_frac, ycur) to the
	 * start of the previous character.
	 *
	 * If not implement, default behaviour will work for fixed-width
	 * characters.
	 */
	int (*backspace)(struct udevice *dev, void *ctx);

	/**
	 * get_font() - Obtain information about a font (optional)
	 *
	 * @dev:	Device to check
	 * @seq:	Font number to query (0=first, 1=second, etc.)
	 * @info:	Returns font information on success
	 * Returns: 0 on success, -ENOENT if no such font
	 */
	int (*get_font)(struct udevice *dev, int seq,
			struct vidfont_info *info);

	/**
	 * get_font_size() - get the current font name and size
	 *
	 * @dev: vidconsole device
	 * @ctx: vidconsole context to use (cannot be NULL)
	 * @sizep: Place to put the font size (nominal height in pixels)
	 * Returns: Current font name
	 */
	const char *(*get_font_size)(struct udevice *dev, void *ctx,
				     uint *sizep);

	/**
	 * select_font() - Select a particular font by name / size
	 *
	 * @dev:	Device to adjust
	 * @ctx:	Context to use
	 * @name:	Font name to use (NULL to use default)
	 * @size:	Font size to use (0 to use default)
	 * Returns: 0 on success, -ENOENT if no such font
	 */
	int (*select_font)(struct udevice *dev, void *ctx, const char *name,
			   uint size);

	/**
	 * measure() - Measure the bounding box of some text
	 *
	 * The text can include newlines
	 *
	 * @dev:	Console device to use
	 * @name:	Font name to use (NULL to use default)
	 * @size:	Font size to use (0 to use default)
	 * @text:	Text to measure
	 * @len:	Number of characters to measure, or -1 for whole string
	 * @limit:	Width limit for each line, or -1 if none
	 * @bbox:	Returns bounding box of text, assuming it is positioned
	 *		at 0,0
	 * @lines:	If non-NULL, this must be an alist of
	 *		struct vidconsole_mline inited by caller. A separate
	 *		record is added for each line of text
	 *
	 * Returns: 0 on success, -ENOENT if no such font
	 */
	int (*measure)(struct udevice *dev, const char *name, uint size,
		       const char *text, int len, int limit,
		       struct vidconsole_bbox *bbox, struct alist *lines);

	/**
	 * nominal() - Measure the expected width of a line of text
	 *
	 * Uses an average font width and nominal height
	 *
	 * @dev: Console device to use
	 * @name: Font name, NULL for default
	 * @size: Font size, ignored if @name is NULL
	 * @num_chars: Number of characters to use
	 * @bbox: Returns nounding box of @num_chars characters
	 * Returns: 0 if OK, -ve on error
	 */
	int (*nominal)(struct udevice *dev, const char *name, uint size,
		       uint num_chars, struct vidconsole_bbox *bbox);

	/**
	 * ctx_new() - Initialise a new context for a client
	 *
	 * Initialises driver-specific fields of a pre-allocated context. The
	 * base vidconsole_ctx fields are already initialised by the uclass.
	 *
	 * @dev: Console device to use
	 * @ctx: Pre-allocated context to initialise
	 * Return: 0 on success, -ve on error
	 */
	int (*ctx_new)(struct udevice *dev, void *ctx);

	/**
	 * ctx_dispose() - Dispose of a context
	 *
	 * Frees any memory allocated for the context.
	 *
	 * @dev: Console device to use
	 * @ctx: Context to dispose of
	 */
	int (*ctx_dispose)(struct udevice *dev, void *ctx);

	/**
	 * get_cursor_info() - Get cursor position info
	 *
	 * Calculates and stores cursor position information. This must fill in
	 * @x, @y, @height and @index using struct vidconsole_priv fields
	 * @xmark_frac, @ymark and @index
	 *
	 * @dev: Console device to use
	 * @ctx: Vidconsole context to use (cannot be NULL)
	 * Return: 0 if OK, -ve on error
	 */
	int (*get_cursor_info)(struct udevice *dev, void *ctx);

	/**
	 * mark_start() - Mark the current position as the state of CLI entry
	 *
	 * This indicates that a new CLI entry is starting, so the user will be
	 * entering characters from this point. The console can use this to set
	 * the beginning point for the cursor.
	 *
	 * @dev: Console device to use
	 * @ctx: Vidconsole context to use (cannot be NULL)
	 */
	int (*mark_start)(struct udevice *dev, void *ctx);
};

/* Get a pointer to the driver operations for a video console device */
#define vidconsole_get_ops(dev)  ((struct vidconsole_ops *)(dev)->driver->ops)

/**
 * vidconsole_ctx() - Get the default context for a vidconsole device
 *
 * @dev: vidconsole device to check
 * Return: pointer to context
 */
void *vidconsole_ctx(struct udevice *dev);

/**
 * vidconsole_ctx_from_priv() - Get the default context for a vidconsole device
 *
 * @priv: vidconsole uclass-private data
 * Return: pointer to context
 */
static inline void *vidconsole_ctx_from_priv(struct vidconsole_priv *uc_priv)
{
	return uc_priv->ctx;
}

/**
 * vidconsole_get_font() - Obtain information about a font
 *
 * @dev:	Device to check
 * @seq:	Font number to query (0=first, 1=second, etc.)
 * @info:	Returns font information on success
 * Returns: 0 on success, -ENOENT if no such font, -ENOSYS if there is no such
 * method
 */
int vidconsole_get_font(struct udevice *dev, int seq,
			struct vidfont_info *info);

/**
 * vidconsole_select_font() - Select a particular font by name / size
 *
 * @dev:	Device to adjust
 * @ctx:	Context to use (NULL to use default)
 * @name:	Font name to use (NULL to use default)
 * @size:	Font size to use (0 to use default)
 */
int vidconsole_select_font(struct udevice *dev, void *ctx, const char *name,
			   uint size);

/**
 * vidconsole_measure() - Measure the bounding box of some text
 *
 * The text can include newlines
 *
 * @dev:	Device to adjust
 * @name:	Font name to use (NULL to use default)
 * @size:	Font size to use (0 to use default)
 * @text:	Text to measure
 * @len:	Number of characters to measure, or -1 for whole string
 * @limit:	Width limit for each line, or -1 if none
 * @bbox:	Returns bounding box of text, assuming it is positioned
 *		at 0,0
 * @lines:	If non-NULL, this must be an alist of
 *		struct vidconsole_mline inited by caller. The list is emptied
 *		and then a separate record is added for each line of text
 *
 * Returns: 0 on success, -ENOENT if no such font
 */
int vidconsole_measure(struct udevice *dev, const char *name, uint size,
		       const char *text, int len, int limit,
		       struct vidconsole_bbox *bbox, struct alist *lines);
/**
 * vidconsole_nominal() - Measure the expected width of a line of text
 *
 * Uses an average font width and nominal height
 *
 * @dev: Console device to use
 * @name: Font name, NULL for default
 * @size: Font size, ignored if @name is NULL
 * @num_chars: Number of characters to use
 * @bbox: Returns nounding box of @num_chars characters
 * Returns: 0 if OK, -ve on error
 */
int vidconsole_nominal(struct udevice *dev, const char *name, uint size,
		       uint num_chars, struct vidconsole_bbox *bbox);

/**
 * vidconsole_ctx_new() - Create a new context for a client
 *
 * Allocates and initialises a context for a client of the vidconsole.
 * The driver determines what information is stored in the context.
 *
 * @dev: Console device to use
 * @ctxp: Returns new context, on success
 * Return: 0 on success, -ENOMEM if out of memory
 */
int vidconsole_ctx_new(struct udevice *dev, void **ctxp);

/**
 * vidconsole_ctx_dispose() - Dispose of a context
 *
 * Frees any memory allocated for the context.
 *
 * @dev: Console device to use
 * @ctx: Context to dispose of
 */
int vidconsole_ctx_dispose(struct udevice *dev, void *ctx);

#ifdef CONFIG_CURSOR
/**
 * vidconsole_show_cursor() - Show the cursor
 *
 * Shows a cursor at the current position.
 *
 * @dev: Console device to use
 * @vctx: Vidconsole context to use, or NULL to use default
 * Return: 0 if OK, -ve on error
 */
int vidconsole_show_cursor(struct udevice *dev, void *vctx);

/**
 * vidconsole_hide_cursor() - Hide the cursor
 *
 * Hides the cursor if it's currently visible
 *
 * @dev: Console device to use
 * @vctx: Vidconsole context to use, or NULL to use default
 * Return: 0 if OK, -ve on error
 */
int vidconsole_hide_cursor(struct udevice *dev, void *vctx);

/**
 * vidconsole_readline_start() - Enable cursor for a video console
 *
 * Called at the start of command line input to show the cursor
 *
 * @dev: vidconsole device
 * @vctx: vidconsole context to use, or NULL to use the default
 * @indent: indent subsequent lines to the same position as the first line
 */
void vidconsole_readline_start(struct udevice *dev, void *vctx, bool indent);

/**
 * vidconsole_readline_end() - Disable cursor for a video console
 *
 * Called at the end of command line input to hide the cursor
 *
 * @dev: vidconsole device
 * @vctx: vidconsole context to use, or NULL to use the default
 */
void vidconsole_readline_end(struct udevice *dev, void *vctx);

/**
 * vidconsole_readline_start_all() - Enable cursor for all video consoles
 *
 * Called at the start of command line input to show cursors on all
 * active video consoles
 *
 * @indent: indent subsequent lines to the same position as the first line
 */
void vidconsole_readline_start_all(bool indent);

/**
 * vidconsole_readline_end_all() - Disable cursor for all video consoles
 *
 * Called at the end of command line input to hide cursors on all
 * active video consoles
 */
void vidconsole_readline_end_all(void);
#else
static inline int vidconsole_show_cursor(struct udevice *dev, void *vctx)
{
	return 0;
}

static inline int vidconsole_hide_cursor(struct udevice *dev, void *vctx)
{
	return 0;
}

static inline void vidconsole_readline_start(struct udevice *dev, void *vctx,
					     bool indent)
{
}

static inline void vidconsole_readline_end(struct udevice *dev, void *vctx)
{
}

static inline void vidconsole_readline_start_all(bool indent)
{
}

static inline void vidconsole_readline_end_all(void)
{
}
#endif /* CONFIG_CURSOR */

static inline void cli_index_adjust(struct vidconsole_ctx *ctx, int by)
{
	if (IS_ENABLED(CONFIG_CURSOR))
		ctx->cli_index += by;
}

/**
 * vidconsole_push_colour() - Temporarily change the font colour
 *
 * @dev:	Device to adjust
 * @fg:		Foreground colour to select
 * @bg:		Background colour to select
 * @old:	Place to store the current colour, so it can be restored
 */
void vidconsole_push_colour(struct udevice *dev, enum colour_idx fg,
			    enum colour_idx bg, struct vidconsole_colour *old);

/**
 * vidconsole_pop_colour() - Restore the original colour
 *
 * @dev:	Device to adjust
 * @old:	Old colour to be restored
 */
void vidconsole_pop_colour(struct udevice *dev, struct vidconsole_colour *old);

/**
 * vidconsole_putc_xy() - write a single character to a position
 *
 * @dev:	Device to write to
 * @ctx:	Vidconsole context to use, or NULL to use default
 * @x_frac:	Fractional pixel X position (0=left-most pixel) which
 *		is the X position multipled by VID_FRAC_DIV.
 * @y:		Pixel Y position (0=top-most pixel)
 * @cp:		UTF-32 code point to write
 * Return: number of fractional pixels that the cursor should move,
 * if all is OK, -EAGAIN if we ran out of space on this line, other -ve
 * on error
 */
int vidconsole_putc_xy(struct udevice *dev, void *ctx, uint x, uint y, int cp);

/**
 * vidconsole_move_rows() - Move text rows from one place to another
 *
 * @dev:	Device to adjust
 * @rowdst:	Destination text row (0=top)
 * @rowsrc:	Source start text row
 * @count:	Number of text rows to move
 * Return: 0 if OK, -ve on error
 */
int vidconsole_move_rows(struct udevice *dev, uint rowdst, uint rowsrc,
			 uint count);

/**
 * vidconsole_set_row() - Set the colour of a text row
 *
 * Every pixel contained within the text row is adjusted
 *
 * @dev:	Device to adjust
 * @row:	Text row to adjust (0=top)
 * @clr:	Raw colour (pixel value) to write to each pixel
 * Return: 0 if OK, -ve on error
 */
int vidconsole_set_row(struct udevice *dev, uint row, int clr);

/**
 * vidconsole_entry_start() - Set the start position of a vidconsole line
 *
 * Marks the current cursor position as the start of a line
 *
 * @dev:	Device to adjust
 * @ctx:	vidconsole context to use, or NULL to use the default
 */
int vidconsole_entry_start(struct udevice *dev, void *ctx);

/**
 * vidconsole_put_char() - Output a character to the current console position
 *
 * Outputs a character to the console and advances the cursor. This function
 * handles wrapping to new lines and scrolling the console. Special
 * characters are handled also: \n, \r, \b and \t.
 *
 * The device always starts with the cursor at position 0,0 (top left). It
 * can be adjusted manually using vidconsole_position_cursor().
 *
 * @dev:	Device to adjust
 * @vctx:	Vidconsole context to use, or NULL to use default
 * @ch:		Character to write
 * Return: 0 if OK, -ve on error
 */
int vidconsole_put_char(struct udevice *dev, void *vctx, char ch);

/**
 * vidconsole_put_stringn() - Output part of a string to the current console pos
 *
 * Outputs part of a string to the console and advances the cursor. This
 * function handles wrapping to new lines and scrolling the console. Special
 * characters are handled also: \n, \r, \b and \t.
 *
 * The device always starts with the cursor at position 0,0 (top left). It
 * can be adjusted manually using vidconsole_position_cursor().
 *
 * @dev:	Device to adjust
 * @ctx:	Vidconsole context, or NULL to use default
 * @str:	String to write
 * @maxlen:	Maximum chars to output, or -1 for all
 * Return: 0 if OK, -ve on error
 */
int vidconsole_put_stringn(struct udevice *dev, void *ctx, const char *str,
			   int maxlen);

/**
 * vidconsole_put_string() - Output a string to the current console position
 *
 * Outputs a string to the console and advances the cursor. This function
 * handles wrapping to new lines and scrolling the console. Special
 * characters are handled also: \n, \r, \b and \t.
 *
 * The device always starts with the cursor at position 0,0 (top left). It
 * can be adjusted manually using vidconsole_position_cursor().
 *
 * @dev:	Device to adjust
 * @ctx:	Vidconsole context, or NULL to use default
 * @str:	String to write
 * Return: 0 if OK, -ve on error
 */
int vidconsole_put_string(struct udevice *dev, void *ctx, const char *str);

/**
 * vidconsole_position_cursor() - Move the text cursor
 *
 * @dev:	Device to adjust
 * @col:	New cursor text column
 * @row:	New cursor text row
 * Return: 0 if OK, -ve on error
 */
void vidconsole_position_cursor(struct udevice *dev, unsigned col,
				unsigned row);

/**
 * vidconsole_clear_and_reset() - Clear the console and reset the cursor
 *
 * The cursor is placed at the start of the console
 *
 * @dev:	vidconsole device to adjust
 */
int vidconsole_clear_and_reset(struct udevice *dev);

/**
 * vidconsole_set_cursor_pos() - set cursor position
 *
 * The cursor is set to the new position and the start-of-line information is
 * updated to the same position, so that a newline will return to @x
 *
 * @dev:	video console device to update
 * @ctx:	vidconsole context to use, or NULL to use the default
 * @x:		x position from left in pixels
 * @y:		y position from top in pixels
 */
void vidconsole_set_cursor_pos(struct udevice *dev, void *ctx, int x, int y);

/**
 * vidconsole_list_fonts() - List the available fonts
 *
 * @dev: vidconsole device to check
 *
 * This shows a list of fonts known by this vidconsole. The list is displayed on
 * the console (not necessarily @dev but probably)
 */
void vidconsole_list_fonts(struct udevice *dev);

/**
 * vidconsole_get_font_size() - get the current font name and size
 *
 * @dev: vidconsole device
 * @ctx: vidconsole context to use (NULL to use default)
 * @sizep: Place to put the font size (nominal height in pixels)
 * @name: pointer to font name, a placeholder for result
 * Return: 0 if OK, -ENOSYS if not implemented in driver
 */
int vidconsole_get_font_size(struct udevice *dev, void *ctx, const char **name,
			     uint *sizep);

/**
 * vidconsole_set_quiet() - Select whether the console should output stdio
 *
 * @dev: vidconsole device
 * @quiet: true to suppress stdout/stderr output, false to enable it
 */
void vidconsole_set_quiet(struct udevice *dev, bool quiet);

/**
 * vidconsole_set_bitmap_font() - prepare vidconsole for chosen bitmap font
 *
 * @dev		vidconsole device
 * @ctx		vidconsole context
 * @fontdata	pointer to font data struct
 */
void vidconsole_set_bitmap_font(struct udevice *dev, struct vidconsole_ctx *ctx,
				struct video_fontdata *fontdata);

/*
 * vidconsole_idle() - Handle periodic cursor display during idle time
 *
 * @dev: vidconsole device
 */
void vidconsole_idle(struct udevice *dev);

#endif
