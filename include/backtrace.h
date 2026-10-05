/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Backtrace support
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#ifndef __BACKTRACE_H
#define __BACKTRACE_H

/* Maximum number of stack frames that can be collected */
#define BACKTRACE_MAX_FRAMES	100

/* Size of buffer for all symbol strings combined */
#define BACKTRACE_SYM_BUFSZ	(4 * 1024)

/**
 * struct backtrace_frame - a single stack frame in a backtrace
 *
 * @addr: return address for this frame
 * @sym: pointer to symbol string in backtrace_ctx->sym_buf, or NULL if not
 *	yet resolved or if sym_buf ran out of space
 */
struct backtrace_frame {
	void *addr;
	char *sym;
};

/**
 * struct backtrace_ctx - context for backtrace operations
 *
 * This structure holds all state for collecting and symbolising a backtrace.
 * It should be declared static to avoid consuming stack space (~5KB).
 *
 * Lifecycle:
 *   1. Call backtrace_init() - fills @frame[].addr with return addresses and
 *      sets @count. The @frame[].sym pointers are initialised to NULL.
 *   2. Call backtrace_get_syms() - resolves addresses to symbol strings,
 *      writing them into @sym_buf and setting @frame[].sym pointers.
 *   3. Access @frame[0..count-1] to read addresses and symbol strings.
 *   4. Call backtrace_uninit() to release resources (currently a no-op).
 *
 * @frame: array of stack frames
 * @count: number of valid entries in @frame
 * @sym_buf: buffer holding NUL-terminated symbol strings packed consecutively
 */
struct backtrace_ctx {
	struct backtrace_frame frame[BACKTRACE_MAX_FRAMES];
	unsigned int count;
	char sym_buf[BACKTRACE_SYM_BUFSZ];
};

/**
 * backtrace_init() - collect a backtrace
 *
 * Collect backtrace addresses into the context. Call backtrace_uninit() when
 * done with the context.
 *
 * @ctx: context to fill
 * @skip: number of stack frames to skip (0 to include backtrace_init itself)
 * Return: number of addresses collected, or -ve on error (e.g. -ENOSYS)
 */
int backtrace_init(struct backtrace_ctx *ctx, unsigned int skip);

/**
 * backtrace_get_syms() - get symbol strings for a backtrace
 *
 * Convert the addresses in the context to symbol strings. The strings are
 * stored in ctx->syms[]. The caller must provide a buffer of sufficient size.
 *
 * @ctx: context with addresses from backtrace_init()
 * @buf: buffer to use for string storage
 * @size: size of buffer in bytes
 * Return: 0 if OK, -ENOSPC if buffer too small
 */
int backtrace_get_syms(struct backtrace_ctx *ctx, char *buf, int size);

/**
 * backtrace_uninit() - free backtrace resources
 *
 * Free any memory allocated in the context.
 *
 * @ctx: context to free
 */
void backtrace_uninit(struct backtrace_ctx *ctx);

/**
 * backtrace_show() - print a backtrace
 *
 * Print a backtrace of the current call stack.
 *
 * Return: 0 if OK, -ve on error
 */
int backtrace_show(void);

/**
 * backtrace_strf() - get a condensed backtrace string into a buffer
 *
 * Return a string containing the last CONFIG_BACKTRACE_SUMMARY_FRAMES function names
 * and line number, separated by ``<-``.
 *
 * For example: ``func_a:12 <-func_b:34 <-func_c:56``
 *
 * @skip: number of stack frames to skip (0 to include backtrace_strf itself)
 * @buf: buffer to write the string to
 * @size: size of buffer
 * Return: pointer to buf, or NULL on error
 */
char *backtrace_strf(unsigned int skip, char *buf, int size);

/**
 * backtrace_str() - get a condensed backtrace string
 *
 * Return a string containing the last CONFIG_BACKTRACE_SUMMARY_FRAMES function names
 * and line number, separated by ``<-``. The string is statically allocated and
 * will be overwritten on the next call.
 *
 * For example: ``func_a:12 <-func_b:34 <-func_c:56``
 *
 * @skip: number of stack frames to skip (0 to include backtrace_str itself)
 * Return: pointer to static string, or NULL on error
 */
#if IS_ENABLED(CONFIG_BACKTRACE)
const char *backtrace_str(unsigned int skip);
#else
static inline const char *backtrace_str(unsigned int skip)
{
	return NULL;
}
#endif

#endif /* __BACKTRACE_H */
