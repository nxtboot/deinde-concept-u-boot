/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Deals with splitting up text output into separate screenfuls
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#ifndef __PAGER_H
#define __PAGER_H

#include <stdbool.h>
#include <abuf.h>
#include <membuf.h>
#include <linux/sizes.h>
#include <linux/string.h>

#define PAGER_BUF_SIZE	SZ_4K

/* Special return value from pager_next() indicating waiting for user input */
#define PAGER_WAITING	((const char *)1)

/* Prompt shown to user when pager reaches page limit */
#define PAGER_PROMPT	"\n: Press SPACE to continue"

/* String used to blank/clear the pager prompt */
#define PAGER_BLANK	"\r                         \r"

/**
 * enum pager_state: Tracks the state of the pager
 *
 * @PAGERST_OK: Normal output is happening
 * @PAGERST_AT_LIMIT: No more output can be provided; the next call to
 * pager_next() will return a user prompt
 * @PAGERST_WAIT_USER: Waiting for the user to press a key
 * @PAGERST_CLEAR_PROMPT: Clearing the prompt ready for more output
 * @PAGERST_BYPASS: Pager is being bypassed
 * @PAGERST_QUIT_SUPPRESS: Output is being suppressed after 'q' keypress
 */
enum pager_state {
	PAGERST_OK,
	PAGERST_AT_LIMIT,
	PAGERST_WAIT_USER,
	PAGERST_CLEAR_PROMPT,
	PAGERST_BYPASS,
	PAGERST_QUIT_SUPPRESS,
};

/**
 * struct pager - pager state
 *
 * The pager uses a buffer @buf to hold text that it is in the process of
 * sending out. This helps deal with the stdio puts() interface, which does not
 * permit passing a string length, only a string, which means that strings must
 * be nul-terminated. The termination is handled automatically by the pager.
 *
 * If the text passed to pager_postn() is too large for @buf then all the next
 * will be written at once, without any paging, in the next call to
 * pager_next().
 *
 * The membuf @mb is initialised one byte smaller than the abuf @buf, so the
 * last byte of @buf is always available for writing a nul terminator. This
 * means pager_next() can safely terminate the returned string without
 * overflowing the underlying allocation.
 *
 * The membuf @mb is only used to feed out text in chunks, with a pager message
 * (and a keypress wait) inserted between each chunk.
 *
 * @line_count: Number of lines output since last pause
 * @page_len: Sets the height of the page in lines. The maximum lines to display
 * before pausing is one less than this. Set from 'pager' env variable
 * @buf: Buffer containing text to eventually be returned
 * @mb: Circular buffer to manage @buf
 * @overflow: pointer to overflow text to send nexts
 * @nulch: pointer to where a nul character was written, NULL if none
 * @oldch: old character that was at @nulch
 * @state: current state of the pager state-machine
 * @test_bypass: true if pager should behave as if in test mode (bypass all)
 */
struct pager {
	int line_count;
	int page_len;
	struct abuf buf;
	struct membuf mb;
	const char *overflow;
	char *nulch;
	int oldch;
	enum pager_state state;
	bool test_bypass;
};

#if IS_ENABLED(CONFIG_CONSOLE_PAGER)

/**
 * pager_postn() - Add text to the input buffer for later handling
 *
 * If @use_pager the text is added to the pager buffer and fed out a screenful
 * at a time. This function calls pager_postn() after storing the text.
 *
 * After calling pager_postn(), if it returns anything other than NULL, you must
 * repeatedly call pager_next() until it returns NULL, otherwise text may be
 * lost
 *
 * If @pag is NULL, this does nothing but return @s
 *
 * @pag: Pager to use, may be NULL
 * @use_pager: Whether or not to use the pager functionality
 * @s: Text to add
 * @len: Length of @s in bytes
 * Return: text which should be sent to output, or NULL if there is no more.
 * If !@use_pager this just returns @s and does not affect the pager state
 */
const char *pager_postn(struct pager *pag, bool use_pager, const char *s,
			int len);

/**
 * pager_post() - Add a nul-terminated string to the pager input buffer
 *
 * Convenience wrapper around pager_postn() for nul-terminated strings.
 *
 * @pag: Pager to use, may be NULL
 * @use_pager: Whether or not to use the pager functionality
 * @s: Nul-terminated text to add
 * Return: text which should be sent to output, or NULL if there is no more
 */
static inline const char *pager_post(struct pager *pag, bool use_pager,
				     const char *s)
{
	return pager_postn(pag, use_pager, s, strlen(s));
}

/**
 * pager_next() - Returns the next screenful of text to show
 *
 * If this function returns PAGER_WAITING then the caller must check for user
 * input and pass in the keypress in the next call to pager_next(). It can
 * busy-wait for a keypress, if desired, since pager_next() will only ever
 * return PAGER_WAITING until @ch is non-zero.
 *
 * When the pager prompts for user input, pressing SPACE continues to the next
 * page, while pressing capital 'Q' puts the pager into bypass mode and
 * disables further paging. Pressing 'q' quits and suppresses all output until
 * the next command prompt.
 *
 * @pag: Pager to use
 * @use_pager: Whether or not to use the pager functionality
 * @ch: Key that the user has pressed, or 0 if none
 *
 * Return: text which should be sent to output, or PAGER_WAITING if waiting for
 * the user to press a key, or NULL if there is no more text.
 * If !@use_pager this just returns NULL and does not affect the pager state
 */
const char *pager_next(struct pager *pag, bool use_pager, int ch);

/**
 * pager_active() - check if pager needs to process output
 *
 * Returns true only when the pager is genuinely active and needs to
 * process output (not bypassed or in test bypass mode).
 *
 * @pag: Pager to check, may be NULL
 * Return: true if the pager is active
 */
static inline bool pager_active(struct pager *pag)
{
	return pag && !pag->test_bypass &&
	       pag->state != PAGERST_BYPASS;
}

/**
 * pager_set_bypass() - put the pager into bypass mode
 *
 * This is used when output is not connected to a terminal. Bypass mode stops
 * the pager from doing anything to interrupt output.
 *
 * @pag: Pager to use, may be NULL in which case this function does nothing
 * @bypass: true to put the pager in bypass mode, false for normal mode
 * Return: old value of the bypass flag
 */
bool pager_set_bypass(struct pager *pag, bool bypass);

/**
 * pager_set_test_bypass() - put the pager into test bypass mode
 *
 * This is used for tests. Test bypass mode stops the pager from doing
 * anything to interrupt output, regardless of the current pager state.
 *
 * @pag: Pager to use, may be NULL in which case this function does nothing
 * @bypass: true to put the pager in test bypass mode, false for normal
 * Return: old value of the test bypass flag
 */
bool pager_set_test_bypass(struct pager *pag, bool bypass);

/**
 * pager_reset() - reset the line count in the pager
 *
 * Sets line_count to zero so that the pager starts afresh with its counting.
 *
 * @pag: Pager to update
 */
void pager_reset(struct pager *pag);

/**
 * pager_clear_quit() - Clear quit suppression mode
 *
 * If the pager is in PAGERST_QUIT_SUPPRESS state, this resets it to normal
 * operation (PAGERST_OK). This is typically called at the start of
 * cli_readline_into_buffer() to allow new commands to display output normally.
 *
 * @pag: Pager to update, may be NULL in which case this function does nothing
 */
void pager_clear_quit(struct pager *pag);

/**
 * pager_uninit() - Uninit the pager
 *
 * Frees all memory and also @pag
 *
 * @pag: Pager to uninit
 */
void pager_uninit(struct pager *pag);

#else
static inline const char *pager_postn(struct pager *pag, bool use_pager,
				      const char *s, int len)
{
	return s;
}

static inline const char *pager_post(struct pager *pag, bool use_pager,
				     const char *s)
{
	return s;
}

static inline const char *pager_next(struct pager *pag, bool use_pager, int ch)
{
	return NULL;
}

static inline bool pager_active(struct pager *pag)
{
	return false;
}

static inline bool pager_set_bypass(struct pager *pag, bool bypass)
{
	return true;
}

static inline bool pager_set_test_bypass(struct pager *pag, bool bypass)
{
	return true;
}

static inline void pager_clear_quit(struct pager *pag)
{
}

static inline void pager_reset(struct pager *pag)
{
}

#endif

/**
 * pager_set_page_len() - Set the page length of a pager
 *
 * @pag: Pager to use
 * @page_len: Page length to set
 */
void pager_set_page_len(struct pager *pag, int page_len);

/**
 * pager_init() - Set up a new pager
 *
 * @pagp: Returns allocaed pager, on success
 * @page_len: Number of lines per page
 * @buf_size: Buffer size to use in bytes, this is the maximum amount of output
 * that can be paged
 * Return: 0 if OK, -ENOMEM if out of memory
 */
int pager_init(struct pager **pagp, int page_len, int buf_size);

#endif
