/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal NLS (National Language Support) stubs for U-Boot
 *
 * Based on the interface from Linux's include/linux/nls.h but heavily
 * simplified: struct nls_table is trimmed to the fields used by isofs,
 * load_nls() and unload_nls() are no-ops, and utf16s_to_utf8s() is a
 * implementation wrapping utf16_to_utf8() in lib/charset.c
 *
 * Joliet support requires NLS for character set conversion. These stubs
 * allow the code to compile without full NLS infrastructure.
 */
#ifndef _LINUX_NLS_H
#define _LINUX_NLS_H

#include <linux/types.h>

#define NLS_MAX_CHARSET_SIZE	6

/* UTF-16 byte order */
enum utf16_endian {
	UTF16_HOST_ENDIAN,
	UTF16_LITTLE_ENDIAN,
	UTF16_BIG_ENDIAN,
};

struct nls_table {
	const char *charset;
	int (*uni2char)(wchar_t uni, unsigned char *out, int boundlen);
	int (*char2uni)(const unsigned char *rawstring, int boundlen,
			wchar_t *uni);
};

static inline struct nls_table *load_nls(const char *charset)
{
	return NULL;
}

static inline struct nls_table *load_nls_default(void)
{
	return NULL;
}

static inline void unload_nls(struct nls_table *nls)
{
}

#include <charset.h>

#endif /* _LINUX_NLS_H */
