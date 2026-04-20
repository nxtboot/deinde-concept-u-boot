// SPDX-License-Identifier: GPL-2.0+
/*
 * Boot Loader Specification (BLS) Type #1 parser
 *
 * Copyright 2026 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#define LOG_CATEGORY UCLASS_BOOTSTD

#include <bls.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <linux/ctype.h>
#include <linux/string.h>

/**
 * enum bls_token_t - BLS Type #1 field tokens
 *
 * Token identifiers for BLS entry fields. Using an enum avoids repeated
 * string comparisons during parsing.
 *
 * Note: The enum values correspond to indices in bls_token_names[]
 */
enum bls_token_t {
	TOK_TITLE = 0,
	TOK_VERSION,
	TOK_LINUX,
	TOK_FIT,
	TOK_OPTIONS,
	TOK_INITRD,
	TOK_DEVICETREE,
	TOK_DEVICETREE_OVERLAY,
	TOK_ARCHITECTURE,
	TOK_MACHINE_ID,
	TOK_SORT_KEY,

	TOK_COUNT,
};

/* BLS field names indexed by enum bls_token_t */
static const char *const bls_token_names[] = {
	[TOK_TITLE]		= "title",
	[TOK_VERSION]		= "version",
	[TOK_LINUX]		= "linux",
	[TOK_FIT]		= "fit",
	[TOK_OPTIONS]		= "options",
	[TOK_INITRD]		= "initrd",
	[TOK_DEVICETREE]	= "devicetree",
	[TOK_DEVICETREE_OVERLAY] = "devicetree-overlay",
	[TOK_ARCHITECTURE]	= "architecture",
	[TOK_MACHINE_ID]	= "machine-id",
	[TOK_SORT_KEY]		= "sort-key",
};

/**
 * bls_lookup_token() - Look up a token by name
 *
 * @key: Field name to look up
 * Return: Token enum value, or TOK_COUNT if not recognized
 */
static enum bls_token_t bls_lookup_token(const char *key)
{
	int i;

	for (i = 0; i < TOK_COUNT; i++) {
		if (!strcmp(key, bls_token_names[i]))
			return i;
	}

	return TOK_COUNT;
}

/**
 * bls_append_str() - Append a string to an existing field
 *
 * Used for fields that can appear multiple times (e.g., options).
 * Concatenates with a space separator.
 *
 * @fieldp: Pointer to field to append to (may be NULL)
 * @value: String to append
 * Return: 0 on success, -ENOMEM if allocation fails
 */
static int bls_append_str(char **fieldp, const char *value)
{
	size_t old_len, val_len, new_len;
	char *new_str;

	if (!*fieldp) {
		*fieldp = strdup(value);
		return *fieldp ? 0 : -ENOMEM;
	}

	old_len = strlen(*fieldp);
	val_len = strlen(value);
	new_len = old_len + 1 + val_len + 1;  /* +1 for space, +1 for nul */

	new_str = realloc(*fieldp, new_len);
	if (!new_str)
		return -ENOMEM;

	new_str[old_len] = ' ';
	memcpy(new_str + old_len + 1, value, val_len + 1);

	*fieldp = new_str;

	return 0;
}

/**
 * bls_skip_whitespace() - Skip leading whitespace
 *
 * @strp: Pointer to string pointer (updated to first non-whitespace char)
 */
static void bls_skip_whitespace(char **strp)
{
	char *p = *strp;

	while (*p && isspace(*p))
		p++;
	*strp = p;
}

/**
 * bls_parse_line() - Parse a single line from a BLS entry file
 *
 * Parses one line of a BLS entry. Lines are in "key value" format where
 * the key and value are separated by whitespace. The value extends to
 * the end of the line.
 *
 * @line: Line to parse (will be modified)
 * @keyp: Returns pointer to key string
 * @valuep: Returns pointer to value string
 * Return: 0 on success, -EINVAL if line is invalid
 */
static int bls_parse_line(char *line, char **keyp, char **valuep)
{
	char *p = line;
	char *key, *value;

	/* Skip leading whitespace */
	bls_skip_whitespace(&p);

	/* Skip blank lines and comments */
	if (!*p || *p == '#')
		return -EINVAL;

	/* Extract key */
	key = p;
	while (*p && !isspace(*p))
		p++;
	if (!*p)
		return -EINVAL;	/* No value */

	*p++ = '\0';

	/* Skip whitespace before value */
	bls_skip_whitespace(&p);
	if (!*p)
		return -EINVAL;	/* No value */

	value = p;

	/* Remove trailing whitespace from value */
	p = value + strlen(value) - 1;
	while (p >= value && isspace(*p))
		*p-- = '\0';

	*keyp = key;
	*valuep = value;

	return 0;
}

int bls_parse_entry(const char *buf, size_t size, struct bls_entry *entry)
{
	char *data = (char *)buf;
	char *line, *next;
	bool err = false;

	/* Initialize entry to zero */
	memset(entry, '\0', sizeof(*entry));

	/* Initialize initrds list */
	alist_init_struct(&entry->initrds, char *);

	log_debug("parsing BLS entry, size %zx\n", size);

	/* Parse buffer line by line, modifying it in place */
	line = data;
	while (line < data + size) {
		enum bls_token_t token;
		char *key, *value;
		int ret;

		/* Find end of line */
		next = memchr(line, '\n', data + size - line);
		if (next) {
			*next = '\0';
			next++;
		} else {
			next = data + size;
		}

		ret = bls_parse_line(line, &key, &value);
		if (ret) {
			line = next;
			continue;	/* Skip blank lines and comments */
		}

		log_debug("BLS field: '%s' = '%s'\n", key, value);
		line = next;

		/* Look up token and parse supported fields */
		token = bls_lookup_token(key);
		switch (token) {
		case TOK_TITLE:
			/* Point into buffer */
			entry->title = value;
			break;
		case TOK_VERSION:
			/* Point into buffer */
			entry->version = value;
			break;
		case TOK_LINUX:
			/* Point into buffer */
			entry->kernel = value;
			break;
		case TOK_FIT:
			/* Point into buffer */
			entry->fit = value;
			break;
		case TOK_OPTIONS:
			/* Multiple times - allocate and concatenate */
			if (bls_append_str(&entry->options, value))
				err = true;
			break;
		case TOK_INITRD:
			/* Multiple times - add pointer to buffer */
			if (!alist_add(&entry->initrds, value))
				err = true;
			break;
		case TOK_DEVICETREE:
			/* Point into buffer */
			entry->devicetree = value;
			break;
		case TOK_DEVICETREE_OVERLAY:
			/* Point into buffer */
			entry->dt_overlays = value;
			break;
		case TOK_ARCHITECTURE:
			/* Point into buffer */
			entry->architecture = value;
			break;
		case TOK_MACHINE_ID:
			/* Point into buffer */
			entry->machine_id = value;
			break;
		case TOK_SORT_KEY:
			/* Point into buffer */
			entry->sort_key = value;
			break;
		default:
			/* Ignore unknown fields for forward compatibility */
			log_debug("Ignoring unknown BLS field: %s\n", key);
			break;
		}
	}

	/* Check for errors during parsing */
	if (err)
		return -ENOMEM;

	/*
	 * Validate required fields: BLS spec requires at least one of
	 * 'linux' or 'efi'. We also accept 'fit' for FIT images.
	 */
	if (!entry->kernel && !entry->fit) {
		log_err("BLS entry missing required 'linux' or 'fit' field\n");
		return -EINVAL;
	}

	return 0;
}

void bls_entry_uninit(struct bls_entry *entry)
{
	if (!entry)
		return;

	/*
	 * Most fields point into the parsed buffer and don't need freeing.
	 * Only options is allocated (for concatenation of multiple lines).
	 */
	free(entry->options);

	/*
	 * Uninit initrds list. The strings in the list point into the buffer,
	 * so we don't free them, just the list structure.
	 */
	alist_uninit(&entry->initrds);
}

