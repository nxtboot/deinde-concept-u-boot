// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * Add to readline cmdline-editing by
 * (C) Copyright 2005
 * JinHua Luo, GuangDong Linux Center, <luo.jinhua@gd-linux.com>
 */

#include <bootretry.h>
#include <cli.h>
#include <command.h>
#include <hang.h>
#include <malloc.h>
#include <pager.h>
#include <time.h>
#include <watchdog.h>
#include <video_console.h>
#include <linux/errno.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

static const char erase_seq[] = "\b \b";	/* erase sequence */
static const char   tab_seq[] = "        ";	/* used to expand TABs */

char console_buffer[CONFIG_SYS_CBSIZE + 1];	/* console I/O buffer	*/

static char *delete_char (char *buffer, char *p, int *colp, int *np, int plen)
{
	char *s;

	if (*np == 0)
		return p;

	if (*(--p) == '\t') {		/* will retype the whole line */
		while (*colp > plen) {
			puts(erase_seq);
			(*colp)--;
		}
		for (s = buffer; s < p; ++s) {
			if (*s == '\t') {
				puts(tab_seq + ((*colp) & 07));
				*colp += 8 - ((*colp) & 07);
			} else {
				++(*colp);
				putc(*s);
			}
		}
	} else {
		puts(erase_seq);
		(*colp)--;
	}
	(*np)--;

	return p;
}

#ifdef CONFIG_CMDLINE_EDITING

/*
 * cmdline-editing related codes from vivi.
 * Author: Janghoon Lyu <nandy@mizi.com>
 */

#define CTL_BACKSPACE		('\b')
#define DEL			((char)255)
#define DEL7			((char)127)
#define CREAD_HIST_CHAR		('!')

#define getcmd_getch()		getchar()

/**
 * cls_putch() - Output a character, using callback if available
 *
 * @cls: CLI line state
 * @ch: Character to output
 */
static void cls_putch(struct cli_line_state *cls, int ch)
{
	struct cli_editor_state *ed = cli_editor(cls);

	if (ed && ed->putch)
		ed->putch(cls, ch);
	else
		putc(ch);
}

static void cls_putnstr(struct cli_line_state *cls, const char *str, size_t n)
{
	while (n-- > 0)
		cls_putch(cls, *str++);
}

#define getcmd_cbeep(cls)	cls_putch(cls, '\a')

#ifdef CONFIG_XPL_BUILD
#define HIST_MAX		3
#define HIST_SIZE		32
#else
#define HIST_MAX		20
#define HIST_SIZE		CONFIG_SYS_CBSIZE
#endif

static int hist_max;
static int hist_add_idx;
static int hist_cur = -1;
static unsigned hist_num;

#ifndef CONFIG_CMD_HISTORY_USE_CALLOC
static char hist_data[HIST_MAX][HIST_SIZE + 1];
#endif
static char *hist_list[HIST_MAX];

#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
#ifdef CONFIG_CMDLINE_UNDO_COUNT
#define UNDO_COUNT	CONFIG_CMDLINE_UNDO_COUNT
#else
#define UNDO_COUNT	64
#endif
#endif

#define add_idx_minus_one() ((hist_add_idx == 0) ? hist_max : hist_add_idx-1)

/**
 * cls_putchars() - Output a character multiple times
 *
 * @cls: CLI line state
 * @count: Number of times to output the character
 * @ch: Character to output
 */
static void cls_putchars(struct cli_line_state *cls, int count, int ch)
{
	int i;

	for (i = 0; i < count; i++)
		cls_putch(cls, ch);
}

static int hist_init(void)
{
	int i;

#ifndef CONFIG_CMD_HISTORY_USE_CALLOC
	for (i = 0; i < HIST_MAX; i++) {
		hist_list[i] = hist_data[i];
		hist_list[i][0] = '\0';
	}
#else
	unsigned char *hist = calloc(HIST_MAX, HIST_SIZE + 1);
	if (!hist)
		panic("%s: calloc: out of memory!\n", __func__);

	for (i = 0; i < HIST_MAX; i++)
		hist_list[i] = hist + (i * (HIST_SIZE + 1));
#endif

	hist_max = 0;
	hist_add_idx = 0;
	hist_cur = -1;
	hist_num = 0;

	return 0;
}

static void cread_add_to_hist(char *line)
{
	strcpy(hist_list[hist_add_idx], line);

	if (++hist_add_idx >= HIST_MAX)
		hist_add_idx = 0;

	if (hist_add_idx > hist_max)
		hist_max = hist_add_idx;

	hist_num++;
}

static char *hist_prev(void)
{
	char *ret;
	int old_cur;

	if (hist_cur < 0)
		return NULL;

	old_cur = hist_cur;
	if (--hist_cur < 0)
		hist_cur = hist_max;

	if (hist_cur == hist_add_idx) {
		hist_cur = old_cur;
		ret = NULL;
	} else {
		ret = hist_list[hist_cur];
	}

	return ret;
}

static char *hist_next(void)
{
	char *ret;

	if (hist_cur < 0)
		return NULL;

	if (hist_cur == hist_add_idx)
		return NULL;

	if (++hist_cur > hist_max)
		hist_cur = 0;

	if (hist_cur == hist_add_idx)
		ret = "";
	else
		ret = hist_list[hist_cur];

	return ret;
}

void cread_print_hist_list(void)
{
	int i;
	uint n;

	n = hist_num - hist_max;

	i = hist_add_idx + 1;
	while (1) {
		if (i > hist_max)
			i = 0;
		if (i == hist_add_idx)
			break;
		printf("%s\n", hist_list[i]);
		n++;
		i++;
	}
}

#define GOTO_LINE_START(target) {		\
	while (cls->num > (target)) {		\
		cls_putch(cls, CTL_BACKSPACE);	\
		cls->num--;			\
	}					\
}

#define ERASE_TO(erase_to) {					\
	if (cls->num < (erase_to)) {				\
		uint wlen = (erase_to) - cls->num;		\
								\
		/* erase characters on screen */		\
		printf("%*s", wlen, "");			\
		while (wlen--)					\
			cls_putch(cls, CTL_BACKSPACE);		\
								\
		/* remove characters from buffer */		\
		memmove(&buf[cls->num], &buf[erase_to],		\
			cls->eol_num - (erase_to) + 1);		\
		cls->eol_num -= (erase_to) - cls->num;		\
	}							\
}

#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
/**
 * cread_start_of_line() - Move cursor to start of line
 *
 * In multiline mode, moves to the character after the previous newline.
 * Otherwise moves to position 0.
 *
 * @cls: CLI line state
 */
static void cread_start_of_line(struct cli_line_state *cls)
{
	struct cli_editor_state *ed = cli_editor(cls);
	uint target = 0;

	if (ed && ed->multiline) {
		char *buf = cls->buf;
		uint i;

		/* find previous newline */
		for (i = cls->num; i > 0; i--) {
			if (buf[i - 1] == '\n') {
				target = i;
				break;
			}
		}
	}
	GOTO_LINE_START(target);
}
#define BEGINNING_OF_LINE() cread_start_of_line(cls)
#else
#define BEGINNING_OF_LINE() GOTO_LINE_START(0)
#endif

#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
static void cread_erase_to_eol(struct cli_line_state *cls)
{
	struct cli_editor_state *ed = cli_editor(cls);
	char *buf = cls->buf;
	uint erase_to;

	if (cls->num >= cls->eol_num)
		return;

	/*
	 * In multiline mode, only erase to end of current line (next newline
	 * or end of buffer)
	 */
	erase_to = cls->eol_num;
	if (ed && ed->multiline) {
		char *nl = strchr(&buf[cls->num], '\n');

		if (nl)
			erase_to = nl - buf;
	}
	ERASE_TO(erase_to);
}
#else
static void cread_erase_to_eol(struct cli_line_state *cls)
{
	if (cls->num < cls->eol_num) {
		printf("%*s", (int)(cls->eol_num - cls->num), "");
		do {
			cls_putch(cls, CTL_BACKSPACE);
		} while (--cls->eol_num > cls->num);
	}
}
#endif

#define GOTO_LINE_END(target) {				\
	if (cls->num < (target)) {			\
		uint wlen = (target) - cls->num;	\
		cls_putnstr(cls, buf + cls->num, wlen);	\
		cls->num = (target);			\
	}						\
}

#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
/**
 * cread_end_of_line() - Move cursor to end of line
 *
 * In multiline mode, moves to the next newline character.
 * Otherwise moves to end of buffer.
 *
 * @cls: CLI line state
 */
static void cread_end_of_line(struct cli_line_state *cls)
{
	struct cli_editor_state *ed = cli_editor(cls);
	char *buf = cls->buf;
	uint target = cls->eol_num;

	if (ed && ed->multiline) {
		char *nl = strchr(&buf[cls->num], '\n');

		if (nl)
			target = nl - buf;
	}
	GOTO_LINE_END(target);
}
#define REFRESH_TO_EOL() cread_end_of_line(cls)
#else
#define REFRESH_TO_EOL() GOTO_LINE_END(cls->eol_num)
#endif

/* undo/redo/yank functions are in cli_undo.c when CMDLINE_EDITOR is enabled */

static void cread_add_char(struct cli_line_state *cls, char ichar, int insert,
			   uint *num, uint *eol_num, char *buf, uint len)
{
	uint wlen;

	/* room ??? */
	if (insert || *num == *eol_num) {
		if (*eol_num > len - 1) {
			getcmd_cbeep(cls);
			return;
		}
		(*eol_num)++;
	}

	/* new edit invalidates redo history */
	cread_clear_redo(cls);

	if (insert) {
		wlen = *eol_num - *num;
		if (wlen > 1)
			memmove(&buf[*num+1], &buf[*num], wlen-1);

		buf[*num] = ichar;
		cls_putnstr(cls, buf + *num, wlen);
		(*num)++;
		while (--wlen)
			cls_putch(cls, CTL_BACKSPACE);
	} else {
		/* echo the character */
		wlen = 1;
		buf[*num] = ichar;
		cls_putnstr(cls, buf + *num, wlen);
		(*num)++;
	}
}

static void cread_add_str(struct cli_line_state *cls, char *str, int strsize,
			  int insert, uint *num, uint *eol_num, char *buf,
			  uint len)
{
	while (strsize--) {
		cread_add_char(cls, *str, insert, num, eol_num, buf, len);
		str++;
	}
}

int cread_line_process_ch(struct cli_line_state *cls, char ichar)
{
	struct cli_editor_state *ed;
	char *buf = cls->buf;

	/* ichar=0x0 when error occurs in U-Boot getc */
	if (!ichar)
		return -EAGAIN;

	if (ichar == '\n') {
		putc('\n');
		buf[cls->eol_num] = '\0';	/* terminate the string */
		return 0;
	}

	switch (ichar) {
	case CTL_CH('a'):
		BEGINNING_OF_LINE();
		break;
	case CTL_CH('c'):	/* ^C - break */
		*buf = '\0';	/* discard input */
		return -EINTR;
	case CTL_CH('f'):
		if (cls->num < cls->eol_num) {
			cls_putch(cls, buf[cls->num]);
			cls->num++;
		}
		break;
	case CTL_CH('b'):
		if (cls->num) {
			cls_putch(cls, CTL_BACKSPACE);
			cls->num--;
		}
		break;
	case CTL_CH('r'):	/* backward-word */
		if (IS_ENABLED(CONFIG_CMDLINE_EDITOR) && cls->num) {
			uint pos = cls->num;

			/* skip spaces before word */
			while (pos > 0 && buf[pos - 1] == ' ')
				pos--;
			/* skip word characters */
			while (pos > 0 && buf[pos - 1] != ' ')
				pos--;
			cls_putchars(cls, cls->num - pos, CTL_BACKSPACE);
			cls->num = pos;
		}
		break;
	case CTL_CH('t'):	/* forward-word */
		if (IS_ENABLED(CONFIG_CMDLINE_EDITOR) && cls->num < cls->eol_num) {
			uint pos = cls->num;

			/* skip spaces after cursor */
			while (pos < cls->eol_num && buf[pos] == ' ') {
				cls_putch(cls, buf[pos]);
				pos++;
			}
			/* skip word characters */
			while (pos < cls->eol_num && buf[pos] != ' ') {
				cls_putch(cls, buf[pos]);
				pos++;
			}
			cls->num = pos;
		}
		break;
	case CTL_CH('d'):
		if (cls->num < cls->eol_num) {
			uint wlen;

			cread_save_undo(cls);
			wlen = cls->eol_num - cls->num - 1;
			if (wlen) {
				memmove(&buf[cls->num], &buf[cls->num + 1],
					wlen);
				cls_putnstr(cls, buf + cls->num, wlen);
			}

			cls_putch(cls, ' ');
			do {
				cls_putch(cls, CTL_BACKSPACE);
			} while (wlen--);
			cls->eol_num--;
		}
		break;
	case CTL_CH('k'): {
		uint erase_to = cls->eol_num;

		ed = cli_editor(cls);
		if (ed && ed->multiline) {
			char *nl = strchr(&buf[cls->num], '\n');

			if (nl)
				erase_to = nl - buf;
		}
		cread_save_undo(cls);
		cread_save_yank(cls, &buf[cls->num], erase_to - cls->num);
		cread_erase_to_eol(cls);
		break;
	}
	case CTL_CH('e'):
		REFRESH_TO_EOL();
		break;
	case CTL_CH('o'):
		cls->insert = !cls->insert;
		break;
	case CTL_CH('w'):
		if (cls->num) {
			uint base, wlen;

			cread_save_undo(cls);
			for (base = cls->num;
			     base > 0 && buf[base - 1] == ' ';)
				base--;
			for (; base > 0 && buf[base - 1] != ' ';)
				base--;

			/* now delete chars from base to cls->num */
			wlen = cls->num - base;
			cread_save_yank(cls, &buf[base], wlen);
			cls->eol_num -= wlen;
			memmove(&buf[base], &buf[cls->num],
				cls->eol_num - base + 1);
			cls->num = base;
			cls_putchars(cls, wlen, CTL_BACKSPACE);
			cls_putnstr(cls, buf + base, cls->eol_num - base);
			cls_putchars(cls, wlen, ' ');
			cls_putchars(cls, wlen + cls->eol_num - cls->num,
				     CTL_BACKSPACE);
		}
		break;
	case CTL_CH('x'):
		if (CONFIG_IS_ENABLED(CONFIG_CMDLINE_UNDO)) {
			cread_save_undo(cls);
			cread_save_yank(cls, buf, cls->eol_num);
			BEGINNING_OF_LINE();
			cread_erase_to_eol(cls);
		}
		break;
	case CTL_CH('y'):
#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
		cread_yank(cls);
#endif
		break;
	case CTL_CH('z'):
		cread_restore_undo(cls);
		break;
	case CTL_CH('g'):
#if IS_ENABLED(CONFIG_CMDLINE_EDITOR)
		cread_redo(cls);
#endif
		break;
	case CTL_CH('u'):
		cread_save_undo(cls);
		cread_save_yank(cls, buf, cls->eol_num);
		BEGINNING_OF_LINE();
		cread_erase_to_eol(cls);
		break;
	case DEL:
	case DEL7:
	case 8:
		if (cls->num) {
			uint wlen;

			cread_save_undo(cls);
			wlen = cls->eol_num - cls->num;
			cls->num--;
			memmove(&buf[cls->num], &buf[cls->num + 1], wlen);
			cls_putch(cls, CTL_BACKSPACE);
			cls_putnstr(cls, buf + cls->num, wlen);
			cls_putch(cls, ' ');
			do {
				cls_putch(cls, CTL_BACKSPACE);
			} while (wlen--);
			cls->eol_num--;
		}
		break;
	case CTL_CH('p'):
	case CTL_CH('n'):
		ed = cli_editor(cls);
		if (ed && ed->multiline && ed->line_nav) {
			int new_num;

			new_num = ed->line_nav(cls, ichar == CTL_CH('p'));
			if (new_num < 0) {
				getcmd_cbeep(cls);
				break;
			}

			/*
			 * Just update the position - the callback handles
			 * cursor display since backspaces don't work across
			 * wrapped line boundaries
			 */
			cls->num = new_num;
			break;
		}
		if (cls->history) {
			char *hline;

			if (ichar == CTL_CH('p'))
				hline = hist_prev();
			else
				hline = hist_next();

			if (!hline) {
				getcmd_cbeep(cls);
				break;
			}

			cread_save_undo(cls);

			/* nuke the current line */
			/* first, go home */
			BEGINNING_OF_LINE();

			/* erase to end of line */
			cread_erase_to_eol(cls);

			/* copy new line into place and display */
			strcpy(buf, hline);
			cls->eol_num = strlen(buf);
			REFRESH_TO_EOL();
			break;
		}
		break;
	case '\t':
		if (IS_ENABLED(CONFIG_AUTO_COMPLETE) && cls->cmd_complete) {
			int num2, col;

			/* do not autocomplete when in the middle */
			if (cls->num < cls->eol_num) {
				getcmd_cbeep(cls);
				break;
			}

			buf[cls->num] = '\0';
			col = strlen(cls->prompt) + cls->eol_num;
			num2 = cls->num;
			cread_save_undo(cls);
			if (cmd_auto_complete(cls->prompt, buf, &num2, &col)) {
				col = num2 - cls->num;
				cls->num += col;
				cls->eol_num += col;
			}
			break;
		}
		fallthrough;
	default:
		cread_add_char(cls, ichar, cls->insert, &cls->num,
			       &cls->eol_num, buf, cls->len);
		break;
	}

	/*
	 * keep the string terminated...if we added a char at the end then we
	 * want a \0 after it
	 */
	buf[cls->eol_num] = '\0';

	return -EAGAIN;
}

void cli_cread_init(struct cli_line_state *cls, char *buf, uint buf_size)
{
	memset(cls, '\0', sizeof(struct cli_line_state));
	cls->insert = true;
	cls->buf = buf;
	cls->len = buf_size;
}

void cli_cread_init_undo(struct cli_line_state *cls, char *buf, uint buf_size)
{
	cli_cread_init(cls, buf, buf_size);
	if (CONFIG_IS_ENABLED(CONFIG_CMDLINE_UNDO)) {
		struct cli_editor_state *ed = cli_editor(cls);

		abuf_init_size(&ed->yank, buf_size);
	}
}

void cli_cread_uninit(struct cli_line_state *cls)
{
	if (IS_ENABLED(CONFIG_CMDLINE_EDITOR)) {
		struct cli_editor_state *ed = cli_editor(cls);
		struct cli_undo_pos *pos;

		alist_for_each(pos, &ed->undo.pos)
			abuf_uninit(&pos->buf);
		alist_uninit(&ed->undo.pos);
		alist_for_each(pos, &ed->redo.pos)
			abuf_uninit(&pos->buf);
		alist_uninit(&ed->redo.pos);
		abuf_uninit(&ed->yank);
	}
}

void cli_cread_add_initial(struct cli_line_state *cls)
{
	int init_len = strlen(cls->buf);

	if (init_len)
		cread_add_str(cls, cls->buf, init_len, 0, &cls->num,
			      &cls->eol_num, cls->buf, cls->len);
}

static int cread_line(const char *const prompt, char *buf, unsigned int *len,
		      int timeout)
{
	struct cli_ch_state s_cch, *cch = &s_cch;
	struct cli_line_state s_cls, *cls = &s_cls;
	char ichar;
	int first = 1;

	cli_ch_init(cch);
	cli_cread_init(cls, buf, *len);
	cli_cread_add_initial(cls);
	cls->prompt = prompt;
	cls->history = true;
	cls->cmd_complete = true;

	while (1) {
		int ret;

		/* Check for saved characters */
		ichar = cli_ch_process(cch, 0);

		if (!ichar) {
			if (bootretry_tstc_timeout())
				return -2;	/* timed out */
			if (first && timeout) {
				u64 etime = endtick(timeout);

				while (!tstc()) {	/* while no incoming data */
					if (get_ticks() >= etime)
						return -2;	/* timed out */
					schedule();
				}
				first = 0;
			}

			ichar = getcmd_getch();
			ichar = cli_ch_process(cch, ichar);
		}

		ret = cread_line_process_ch(cls, ichar);
		if (ret == -EINTR)
			return -1;
		else if (!ret)
			break;
	}
	*len = cls->eol_num;

	if (buf[0] && buf[0] != CREAD_HIST_CHAR)
		cread_add_to_hist(buf);
	hist_cur = hist_add_idx;

	return 0;
}

#else /* !CONFIG_CMDLINE_EDITING */

static inline int hist_init(void)
{
	return 0;
}

static int cread_line(const char *const prompt, char *buf, unsigned int *len,
		      int timeout)
{
	return 0;
}

#endif /* CONFIG_CMDLINE_EDITING */

/****************************************************************************/

int cli_readline(const char *const prompt)
{
	/*
	 * If console_buffer isn't 0-length the user will be prompted to modify
	 * it instead of entering it from scratch as desired.
	 */
	console_buffer[0] = '\0';

	return cli_readline_into_buffer(prompt, console_buffer, 0);
}

/**
 * cread_line_simple() - Simple (small) command-line reader
 *
 * This supports only basic editing, with no cursor movement
 *
 * @prompt: Prompt to display
 * @p: Text buffer to edit
 * Return: length of text buffer, or -1 if input was cannncelled (Ctrl-C)
 */
static int cread_line_simple(const char *const prompt, char *p)
{
	char *p_buf = p;
	int n = 0;		/* buffer index */
	int plen = 0;		/* prompt length */
	int col;		/* output column cnt */
	int c;

	/* print prompt */
	if (prompt) {
		plen = strlen(prompt);
		puts(prompt);
	}
	col = plen;

	for (;;) {
		if (bootretry_tstc_timeout())
			return -2;	/* timed out */
		schedule();	/* Trigger watchdog, if needed */

		c = getchar();

		/*
		 * Special character handling
		 */
		switch (c) {
		case '\r':			/* Enter		*/
		case '\n':
			*p = '\0';
			puts("\r\n");
			return p - p_buf;

		case '\0':			/* nul			*/
			continue;

		case 0x03:			/* ^C - break		*/
			p_buf[0] = '\0';	/* discard input */
			return -1;

		case 0x15:			/* ^U - erase line	*/
			while (col > plen) {
				puts(erase_seq);
				--col;
			}
			p = p_buf;
			n = 0;
			continue;

		case 0x17:			/* ^W - erase word	*/
			p = delete_char(p_buf, p, &col, &n, plen);
			while ((n > 0) && (*p != ' '))
				p = delete_char(p_buf, p, &col, &n, plen);
			continue;

		case 0x08:			/* ^H  - backspace	*/
		case 0x7F:			/* DEL - backspace	*/
			p = delete_char(p_buf, p, &col, &n, plen);
			continue;

		default:
			/* Must be a normal character then */
			if (n >= CONFIG_SYS_CBSIZE - 2) { /* Buffer full */
				putc('\a');
				break;
			}
			if (c == '\t') {	/* expand TABs */
				if (IS_ENABLED(CONFIG_AUTO_COMPLETE)) {
					/*
					 * if auto-completion triggered just
					 * continue
					 */
					*p = '\0';
					if (cmd_auto_complete(prompt,
							      console_buffer,
							      &n, &col)) {
						p = p_buf + n;	/* reset */
						continue;
					}
				}
				puts(tab_seq + (col & 07));
				col += 8 - (col & 07);
			} else {
				char __maybe_unused buf[2];

				/*
				 * Echo input using puts() to force an LCD
				 * flush if we are using an LCD
				 */
				++col;
				buf[0] = c;
				buf[1] = '\0';
				puts(buf);
			}
			*p++ = c;
			++n;
			break;
		}
	}
}

int cli_readline_into_buffer(const char *const prompt, char *buffer,
			     int timeout)
{
	char *p = buffer;
	uint len = CONFIG_SYS_CBSIZE;
	int rc;
	static int initted;
	bool old_bypass;

	pager_clear_quit(gd_pager());
	old_bypass = pager_set_bypass(gd_pager(), true);

	/*
	 * Say N to CMD_HISTORY_USE_CALLOC will skip runtime
	 * allocation for the history buffer and directly
	 * use an uninitialized static array as the buffer.
	 * Doing this might have better performance and not
	 * increase the binary file's size, as it only marks
	 * the size. However, the array is only writable after
	 * relocation to RAM. If u-boot is running from ROM
	 * all the time, consider say Y to CMD_HISTORY_USE_CALLOC
	 * or disable CMD_HISTORY.
	 */
	if (IS_ENABLED(CONFIG_CMDLINE_EDITING) && (gd->flags & GD_FLG_RELOC)) {
		if (!initted) {
			rc = hist_init();
			if (rc == 0)
				initted = 1;
		}

		if (prompt)
			puts(prompt);

		/* tell the vidconsole the cursor is at its start position */
		vidconsole_readline_start_all(false);
		rc = cread_line(prompt, p, &len, timeout);
		rc = rc < 0 ? rc : len;

	} else {
		rc = cread_line_simple(prompt, p);
	}

	pager_set_bypass(gd_pager(), old_bypass);
	pager_reset(gd_pager());

	vidconsole_readline_end_all();

	return rc;
}
