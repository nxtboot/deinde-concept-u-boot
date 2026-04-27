// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 */

#define LOG_CATEGORY	LOGC_BOOT

#include <ctype.h>
#include <malloc.h>
#include <mapmem.h>
#include "pxe_utils.h"

/** enum token_type - Tokens for the pxe file parser */
enum token_type {
	T_EOL,
	T_STRING,
	T_EOF,
	T_MENU,
	T_TITLE,
	T_TIMEOUT,
	T_LABEL,
	T_KERNEL,
	T_LINUX,
	T_APPEND,
	T_INITRD,
	T_LOCALBOOT,
	T_DEFAULT,
	T_PROMPT,
	T_INCLUDE,
	T_FDT,
	T_FDTDIR,
	T_FDTOVERLAYS,
	T_ONTIMEOUT,
	T_IPAPPEND,
	T_BACKGROUND,
	T_KASLRSEED,
	T_FALLBACK,
	T_SAY,
	T_FIT,
	T_INVALID
};

/** struct token - token - given by a value and a type */
struct token {
	char *val;
	enum token_type type;
};

/* Keywords recognized */
static const struct token keywords[] = {
	{"menu", T_MENU},
	{"title", T_TITLE},
	{"timeout", T_TIMEOUT},
	{"default", T_DEFAULT},
	{"prompt", T_PROMPT},
	{"label", T_LABEL},
	{"kernel", T_KERNEL},
	{"linux", T_LINUX},
	{"localboot", T_LOCALBOOT},
	{"append", T_APPEND},
	{"initrd", T_INITRD},
	{"include", T_INCLUDE},
	{"devicetree", T_FDT},
	{"fdt", T_FDT},
	{"devicetreedir", T_FDTDIR},
	{"fdtdir", T_FDTDIR},
	{"fdtoverlays", T_FDTOVERLAYS},
	{"devicetree-overlay", T_FDTOVERLAYS},
	{"ontimeout", T_ONTIMEOUT,},
	{"ipappend", T_IPAPPEND,},
	{"background", T_BACKGROUND,},
	{"kaslrseed", T_KASLRSEED,},
	{"fallback", T_FALLBACK,},
	{"say", T_SAY,},
	{"fit", T_FIT,},
	{NULL, T_INVALID}
};

/**
 * enum lex_state - lexer state
 *
 * Since pxe(linux) files don't have a token to identify the start of a
 * literal, we have to keep track of when we're in a state where a literal is
 * expected vs when we're in a state a keyword is expected.
 */
enum lex_state {
	L_NORMAL = 0,
	L_KEYWORD,
	L_SLITERAL
};

/**
 * label_create() - crate a new PXE label
 *
 * Allocates memory for and initializes a pxe_label. This uses malloc, so the
 * result must be free()'d to reclaim the memory.
 *
 * Returns a pointer to the label, or NULL if out of memory
 */
static struct pxe_label *label_create(void)
{
	struct pxe_label *label;

	label = malloc(sizeof(struct pxe_label));
	if (!label)
		return NULL;
	memset(label, 0, sizeof(struct pxe_label));
	alist_init_struct(&label->files, struct pxe_file);
	if (IS_ENABLED(CONFIG_PXE_INITRD_LIST))
		alist_init(&label->initrds, sizeof(char *), 4);

	return label;
}

void label_destroy(struct pxe_label *label)
{
	struct pxe_file *file;

	free(label->name);
	free(label->menu);
	free(label->kernel_label);
	free(label->kernel);
	free(label->config);
	free(label->append);
	if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
		const char **initrd;

		alist_for_each(initrd, &label->initrds)
			free((void *)*initrd);
		alist_uninit(&label->initrds);
	} else {
		free(label->initrd);
	}
	free(label->fdt);
	free(label->fdtdir);
	alist_for_each(file, &label->files)
		free(file->path);
	alist_uninit(&label->files);
	free(label->say);
	free(label);
}

/**
 * get_string() - retrieves a string from *p and stores it as a token in *t.
 *
 * This is used for scanning both string literals and keywords.
 *
 * Characters from *p are copied into t-val until a character equal to
 * delim is found, or a NUL byte is reached. If delim has the special value of
 * ' ', any whitespace character will be used as a delimiter.
 *
 * If lower is unequal to 0, uppercase characters will be converted to
 * lowercase in the result. This is useful to make keywords case
 * insensitive.
 *
 * The location of *p is updated to point to the first character after the end
 * of the token - the ending delimiter.
 *
 * Memory for t->val is allocated using malloc and must be free()'d to reclaim
 * it.
 *
 * @p: Points to a pointer to the current position in the input being processed.
 *	Updated to point at the first character after the current token
 * @t: Pointers to a token to fill in
 * @delim: Delimiter character to look for, either newline or space
 * @lower: true to convert the string to lower case when storing
 * @limit: End of buffer (position of nul terminator)
 * Returns the new value of t->val, on success, NULL if out of memory
 */
static char *get_string(char **p, struct token *t, char delim, int lower,
			const char *limit)
{
	char *b, *e;
	size_t len, i;

	/*
	 * b and e both start at the beginning of the input stream.
	 *
	 * e is incremented until we find the ending delimiter, or a NUL byte
	 * is reached. Then, we take e - b to find the length of the token.
	 */
	b = *p;
	e = *p;
	while (*e && e < limit) {
		if ((delim == ' ' && isspace(*e)) || delim == *e)
			break;
		e++;
	}
	len = e - b;

	/*
	 * Allocate memory to hold the string, and copy it in, converting
	 * characters to lowercase if lower is != 0.
	 */
	t->val = malloc(len + 1);
	if (!t->val)
		return NULL;

	for (i = 0; i < len; i++, b++) {
		if (lower)
			t->val[i] = tolower(*b);
		else
			t->val[i] = *b;
	}

	t->val[len] = '\0';

	/* Update *p so the caller knows where to continue scanning */
	*p = e;
	t->type = T_STRING;

	return t->val;
}

/**
 * get_keyword() - Populate a keyword token with a type and value
 *
 * Updates the ->type field based on the keyword string in @val
 * @t: Token to populate
 */
static void get_keyword(struct token *t)
{
	int i;

	for (i = 0; keywords[i].val; i++) {
		if (!strcmp(t->val, keywords[i].val)) {
			t->type = keywords[i].type;
			break;
		}
	}
}

/**
 * get_token() - Get the next token
 *
 * We have to keep track of which state we're in to know if we're looking to get
 * a string literal or a keyword.
 *
 * @p: Points to a pointer to the current position in the input being processed.
 *	Updated to point at the first character after the current token
 * @t: Token to fill in
 * @state: Lexer state (keyword or string literal)
 * @limit: End of buffer (position of nul terminator)
 */
static void get_token(char **p, struct token *t, enum lex_state state,
		      const char *limit)
{
	char *c = *p;

	t->type = T_INVALID;
	t->val = NULL;

	/* eat non EOL whitespace */
	while (isblank(*c))
		c++;

	/*
	 * eat comments. note that string literals can't begin with #, but
	 * can contain a # after their first character.
	 */
	if (*c == '#') {
		while (*c && *c != '\n')
			c++;
	}

	if (*c == '\n') {
		t->type = T_EOL;
		c++;
	} else if (*c == '\0' || c >= limit) {
		t->type = T_EOF;
		c++;
	} else if (state == L_SLITERAL) {
		get_string(&c, t, '\n', 0, limit);
	} else if (state == L_KEYWORD) {
		/*
		 * when we expect a keyword, we first get the next string
		 * token delimited by whitespace, and then check if it
		 * matches a keyword in our keyword list. if it does, it's
		 * converted to a keyword token of the appropriate type, and
		 * if not, it remains a string token.
		 */
		get_string(&c, t, ' ', 1, limit);
		get_keyword(t);
	}

	*p = c;
}

/**
 * eol_or_eof() - Find end of line
 *
 * Increment *c until we get to the end of the current line, or EOF
 *
 * @c: Points to a pointer to the current position in the input being processed.
 *	Updated to point at the first character after the current token
 */
static void eol_or_eof(char **c)
{
	while (**c && **c != '\n')
		(*c)++;
}

/*
 * All of these parse_* functions share some common behavior.
 *
 * They finish with *c pointing after the token they parse, and return 1 on
 * success, or < 0 on error.
 */

/*
 * Parse a string literal and store a pointer to it at *dst. String literals
 * terminate at the end of the line.
 */
static int parse_sliteral(char **c, char **dst, const char *limit)
{
	struct token t;
	char *s = *c;

	get_token(c, &t, L_SLITERAL, limit);
	if (t.type != T_STRING) {
		printf("Expected string literal: %.*s\n", (int)(*c - s), s);
		return -EINVAL;
	}

	*dst = t.val;

	return 1;
}

/*
 * Check if a files list contains any FDT overlays.
 */
static bool has_fdtoverlays(struct alist *files)
{
	struct pxe_file *file;

	alist_for_each(file, files) {
		if (file->type == PFT_FDTOVERLAY)
			return true;
	}

	return false;
}

/**
 * label_add_file() - Add a file to a label's file list
 *
 * @label: Label to add file to
 * @path: Path to file (will be duplicated)
 * @type: Type of file (PFT_KERNEL, PFT_INITRD, etc.)
 * Return: 0 on success, -ENOMEM on allocation failure
 */
static int label_add_file(struct pxe_label *label, const char *path,
			  enum pxe_file_type_t type)
{
	struct pxe_file item;

	item.path = strdup(path);
	if (!item.path)
		return -ENOMEM;
	item.type = type;
	item.addr = 0;
	item.size = 0;
	if (!alist_add(&label->files, item)) {
		free(item.path);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Parse a space-separated list of overlay paths into a label's file list.
 */
static int parse_fdtoverlays(char **c, struct pxe_label *label,
			     const char *limit)
{
	char *val, *start;
	int err;

	err = parse_sliteral(c, &val, limit);
	if (err < 0)
		return err;
	start = val;

	while (*val) {
		char *end;

		/* Skip leading spaces */
		while (*val == ' ')
			val++;

		if (!*val)
			break;

		/* Find end of this path and temporarily null-terminate */
		end = strchr(val, ' ');
		if (end)
			*end = '\0';

		err = label_add_file(label, val, PFT_FDTOVERLAY);
		if (err) {
			free(start);
			return err;
		}

		if (end) {
			*end = ' ';
			val = end + 1;
		} else {
			break;
		}
	}

	free(start);

	return 1;
}

/*
 * Parse a base 10 (unsigned) integer and store it at *dst.
 */
static int parse_integer(char **c, int *dst, const char *limit)
{
	struct token t;
	char *s = *c;

	get_token(c, &t, L_SLITERAL, limit);
	if (t.type != T_STRING) {
		printf("Expected string: %.*s\n", (int)(*c - s), s);
		return -EINVAL;
	}

	*dst = simple_strtol(t.val, NULL, 10);
	free(t.val);

	return 1;
}

/*
 * Parse an include statement and store the path for later loading.
 *
 * The include is added to cfg->includes. The caller is responsible for
 * loading these files and calling pxe_parse_include() to parse them.
 */
static int handle_include(char **c, struct pxe_menu *cfg, int nest_level,
			  const char *limit)
{
	struct pxe_include inc;
	char *s = *c;
	int err;

	err = parse_sliteral(c, &inc.path, limit);
	if (err < 0) {
		printf("Expected include path: %.*s\n", (int)(*c - s), s);
		return err;
	}
	inc.cfg = cfg;
	inc.nest_level = nest_level + 1;

	if (!alist_add(&cfg->includes, inc)) {
		free(inc.path);
		return -ENOMEM;
	}

	return 1;
}

/*
 * Parse lines that begin with 'menu'.
 *
 * base and nest are provided to handle the 'menu include' case.
 *
 * base should point to a location where it's safe to store the included file.
 *
 * nest_level should be 1 when parsing the top level pxe file, 2 when parsing
 * a file it includes, 3 when parsing a file included by that file, and so on.
 */
static int parse_menu(struct pxe_context *ctx, char **c, struct pxe_menu *cfg,
		      int nest_level, const char *limit)
{
	struct token t;
	char *s = *c;
	int err = 0;

	t.val = NULL;
	get_token(c, &t, L_KEYWORD, limit);

	switch (t.type) {
	case T_TITLE:
		err = parse_sliteral(c, &cfg->title, limit);
		break;
	case T_INCLUDE:
		err = handle_include(c, cfg, nest_level, limit);
		break;
	case T_BACKGROUND:
		err = parse_sliteral(c, &cfg->bmp, limit);
		break;
	default:
		if (!ctx->quiet)
			printf("Ignoring malformed menu command: %.*s\n",
			       (int)(*c - s), s);
	}
	free(t.val);
	if (err < 0)
		return err;

	eol_or_eof(c);

	return 1;
}

/*
 * Handles parsing a 'menu line' when we're parsing a label.
 */
static int parse_label_menu(struct pxe_context *ctx, char **c,
			    struct pxe_menu *cfg, struct pxe_label *label,
			    const char *limit)
{
	struct token t;
	char *s;

	s = *c;
	t.val = NULL;
	get_token(c, &t, L_KEYWORD, limit);

	switch (t.type) {
	case T_DEFAULT:
		if (!cfg->default_label)
			cfg->default_label = strdup(label->name);

		if (!cfg->default_label)
			return -ENOMEM;

		break;
	case T_LABEL:
		parse_sliteral(c, &label->menu, limit);
		break;
	default:
		if (!ctx->quiet)
			printf("Ignoring malformed menu command: %.*s\n",
			       (int)(*c - s), s);
	}

	free(t.val);
	eol_or_eof(c);

	return 0;
}

/*
 * Handles parsing a 'kernel' label.
 * expecting "filename" or "<fit_filename>#cfg"
 */
static int parse_label_kernel(char **c, struct pxe_label *label,
			      const char *limit)
{
	char *s;
	int err;

	err = parse_sliteral(c, &label->kernel, limit);
	if (err < 0)
		return err;

	/* copy the kernel label to compare with FDT / INITRD when FIT is used */
	label->kernel_label = strdup(label->kernel);
	if (!label->kernel_label)
		return -ENOMEM;

	s = strstr(label->kernel, "#");
	if (s) {
		label->config = strdup(s);
		if (!label->config)
			return -ENOMEM;

		*s = 0;
	}

	return label_add_file(label, label->kernel, PFT_KERNEL) ? : 1;
}

/*
 * Parses a label and adds it to the list of labels for a menu.
 *
 * A label ends when we either get to the end of a file, or
 * get some input we otherwise don't have a handler defined
 * for.
 */
static int parse_label(struct pxe_context *ctx, char **c, struct pxe_menu *cfg,
		       const char *limit)
{
	struct token t;
	int len;
	char *s = *c;
	char *initrd_path;
	struct pxe_label *label;
	int err;

	label = label_create();
	if (!label)
		return -ENOMEM;

	err = parse_sliteral(c, &label->name, limit);
	if (err < 0) {
		printf("Expected label name: %.*s\n", (int)(*c - s), s);
		label_destroy(label);
		return -EINVAL;
	}
	list_add_tail(&label->list, &cfg->labels);

	t.val = NULL;
	while (1) {
		s = *c;
		free(t.val);
		get_token(c, &t, L_KEYWORD, limit);

		err = 0;
		switch (t.type) {
		case T_MENU:
			err = parse_label_menu(ctx, c, cfg, label, limit);
			break;
		case T_KERNEL:
		case T_LINUX:
		case T_FIT:
			err = parse_label_kernel(c, label, limit);
			break;
		case T_APPEND:
			err = parse_sliteral(c, &label->append, limit);
			if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
				if (label->initrds.count)
					break;
			} else {
				if (label->initrd)
					break;
			}
			s = strstr(label->append, "initrd=");
			if (!s)
				break;
			s += 7;
			len = (int)(strchr(s, ' ') - s);
			initrd_path = malloc(len + 1);
			if (!initrd_path) {
				err = -ENOMEM;
				break;
			}
			strlcpy(initrd_path, s, len + 1);
			if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
				if (!alist_add(&label->initrds, initrd_path)) {
					free(initrd_path);
					err = -ENOMEM;
					break;
				}
			} else {
				label->initrd = initrd_path;
			}
			err = label_add_file(label, initrd_path, PFT_INITRD);

			break;
		case T_INITRD:
			if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
				if (label->initrds.count)
					break;
			} else {
				if (label->initrd)
					break;
			}
			err = parse_sliteral(c, &initrd_path, limit);
			if (err < 0)
				break;
			if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
				if (!alist_add(&label->initrds, initrd_path)) {
					free(initrd_path);
					err = -ENOMEM;
					break;
				}
			} else {
				label->initrd = initrd_path;
			}
			err = label_add_file(label, initrd_path, PFT_INITRD);
			break;
		case T_FDT:
			if (!label->fdt) {
				err = parse_sliteral(c, &label->fdt, limit);
				if (err < 0)
					break;
				err = label_add_file(label, label->fdt, PFT_FDT);
			}
			break;
		case T_FDTDIR:
			if (!label->fdtdir)
				err = parse_sliteral(c, &label->fdtdir, limit);
			break;
		case T_FDTOVERLAYS:
			if (!has_fdtoverlays(&label->files))
				err = parse_fdtoverlays(c, label, limit);
			break;
		case T_LOCALBOOT:
			label->localboot = 1;
			err = parse_integer(c, &label->localboot_val, limit);
			break;
		case T_IPAPPEND:
			err = parse_integer(c, &label->ipappend, limit);
			break;
		case T_KASLRSEED:
			label->kaslrseed = 1;
			break;
		case T_EOL:
			break;
		case T_SAY: {
			char *p = strchr(s, '\n');

			if (p) {
				label->say = strndup(*c + 1, p - *c - 1);
				if (!label->say) {
					free(t.val);
					return -ENOMEM;
				}
				*c = p;
			}
			break;
		}
		default:
			/*
			 * put the token back! we don't want it - it's the end
			 * of a label and whatever token this is, it's
			 * something for the menu level context to handle.
			 */
			*c = s;
			free(t.val);
			return 1;
		}

		if (err < 0) {
			free(t.val);
			return err;
		}
	}
}

/*
 * This 16 comes from the limit pxelinux imposes on nested includes.
 *
 * There is no reason at all we couldn't do more, but some limit helps prevent
 * infinite (until crash occurs) recursion if a file tries to include itself.
 */
#define MAX_NEST_LEVEL 16

int parse_pxefile_top(struct pxe_context *ctx, char *p, const char *limit,
		      struct pxe_menu *cfg, int nest_level)
{
	struct token t;
	char *s, *label_name;
	int err;

	if (nest_level > MAX_NEST_LEVEL) {
		printf("Maximum nesting (%d) exceeded\n", MAX_NEST_LEVEL);
		return -EMLINK;
	}

	t.val = NULL;
	while (1) {
		s = p;
		free(t.val);
		get_token(&p, &t, L_KEYWORD, limit);

		err = 0;
		switch (t.type) {
		case T_MENU:
			cfg->prompt = 1;
			err = parse_menu(ctx, &p, cfg, nest_level, limit);
			break;
		case T_TIMEOUT:
			err = parse_integer(&p, &cfg->timeout, limit);
			break;
		case T_LABEL:
			err = parse_label(ctx, &p, cfg, limit);
			break;
		case T_DEFAULT:
		case T_ONTIMEOUT:
			err = parse_sliteral(&p, &label_name, limit);
			if (err >= 0 && label_name) {
				if (cfg->default_label)
					free(cfg->default_label);

				cfg->default_label = label_name;
			}
			break;
		case T_FALLBACK:
			err = parse_sliteral(&p, &label_name, limit);
			if (err >= 0 && label_name) {
				if (cfg->fallback_label)
					free(cfg->fallback_label);

				cfg->fallback_label = label_name;
			}
			break;
		case T_INCLUDE:
			err = handle_include(&p, cfg, nest_level, limit);
			break;
		case T_PROMPT:
			err = parse_integer(&p, &cfg->prompt, limit);
			// Do not fail if prompt configuration is undefined
			if (err <  0)
				eol_or_eof(&p);
			break;
		case T_EOL:
			break;
		case T_EOF:
			free(t.val);
			return 1;
		default:
			if (!ctx->quiet)
				printf("Ignoring unknown command: %.*s\n",
				       (int)(p - s), s);
			eol_or_eof(&p);
		}

		if (err < 0) {
			free(t.val);
			return err;
		}
	}
}
