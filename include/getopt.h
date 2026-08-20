/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * getopt.h - a simple getopt(3) implementation.
 *
 * Copyright (C) 2020 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2007 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#ifndef __GETOPT_H
#define __GETOPT_H

#include <stdbool.h>
#include <stddef.h>
#include <linux/kconfig.h>

/**
 * struct getopt_state - Saved state across getopt() calls
 */
struct getopt_state {
#ifdef CONFIG_GETOPT_PERMUTE
	/**
	 * @argv: Working copy of argv. getopt() reorders this in place: as
	 * options are consumed, non-options are permuted to the end.
	 */
	char *argv[CONFIG_SYS_MAXARGS + 1];
#else
	/**
	 * @argv: Borrowed pointer to the caller's argv. In POSIX mode getopt()
	 * never reorders, so no writable copy is needed.
	 */
	char *const *argv;
#endif
	/** @argc: Argument count, as passed to getopt_init_state() */
	int argc;
	/**
	 * @index: Index of the next unparsed argument of @argv. If getopt()
	 * has parsed all of @argv, then @index will be the first non-option
	 * argument of @argv (or @argc if none).
	 */
	int index;
	/** @arg_index: Index within the current argument */
	int arg_index;
	/**
	 * @cmd_flag: Flags for the command being run, such as CMD_FLAG_REPEAT,
	 * as passed to cmd_invoke(). This lets a command tell a repeat from a
	 * fresh invocation. It is zero when the state was set up by hand rather
	 * than to run a command.
	 */
	int cmd_flag;
#ifdef CONFIG_GETOPT_PERMUTE
	/** @nonopts: Number of non-option arguments in @argv. */
	int nonopts;
#endif
	union {
		/**
		 * @opt: Option being parsed when an error occurs. @opt is only
		 * valid when getopt() returns ``?`` or ``:``.
		 */
		int opt;
		/**
		 * @arg: The argument to an option, NULL if there is none. @arg
		 * is only valid when getopt() returns an option character.
		 */
		char *arg;
	};
};

/**
 * getopt_init_state() - Initialize a &struct getopt_state
 * @gs: The state to initialize
 * @argc: Argument count
 * @argv: Source argv. Copied into @gs->argv; the original is not
 *        modified. @argc must not exceed ``CONFIG_SYS_MAXARGS``;
 *        excess arguments are silently dropped.
 *
 * This must be called before using @gs with getopt().
 */
void getopt_init_state(struct getopt_state *gs, int argc,
		       char *const argv[]);

/**
 * getopt() - Parse short command-line options
 * @gs: Internal state and out-of-band return arguments. This must be
 *      initialized with getopt_init_state() beforehand.
 * @optstring: Option specification, as described below
 *
 * getopt() parses short options. Short options are single characters. They may
 * be followed by a required argument or an optional argument. Arguments to
 * options may occur in the same argument as an option (like ``-larg``), or
 * in the following argument (like ``-l arg``). An argument containing
 * options begins with a ``-``. If an option expects no arguments, then it may
 * be immediately followed by another option (like ``ls -alR``).
 *
 * @optstring is a list of accepted options. If an option is followed by ``:``
 * in @optstring, then it expects a mandatory argument. If an option is followed
 * by ``::`` in @optstring, it expects an optional argument. @gs.arg points
 * to the argument, if one is parsed.
 *
 * By default, getopt() permutes its argv: when it encounters a non-option,
 * it moves that element to the end of @gs.argv and keeps scanning, so
 * options can appear anywhere on the command line. After parsing finishes,
 * @gs.index points at the first non-option, and there are @gs.nonopts of
 * them at @gs.argv[gs.index..argc-1]. If @optstring begins with ``+``,
 * getopt() instead stops at the first non-option (POSIX ``getopt``
 * behaviour). An ``--`` argument terminates option scanning in either mode.
 *
 * getopt() does not print any error messages; the caller is expected to
 * detect '?' and ':' and report via its own usage string (typically
 * CMD_RET_USAGE).
 *
 * An example invocation of getopt() might look like::
 *
 *     char *argv[] = { "program", "-cbx", "-a", "foo", "bar", 0 };
 *     int opt, argc = ARRAY_SIZE(argv) - 1;
 *     struct getopt_state gs;
 *
 *     getopt_init_state(&gs, argc, argv);
 *     while ((opt = getopt(&gs, "a::b:c")) != -1)
 *         printf("opt = %c, index = %d, arg = \"%s\"\n", opt, gs.index, gs.arg);
 *     printf("%d argument(s) left\n", gs.nonopts);
 *
 * and would produce an output of::
 *
 *     opt = c, index = 1, arg = "<NULL>"
 *     opt = b, index = 2, arg = "x"
 *     opt = a, index = 3, arg = "foo"
 *     1 argument(s) left
 *
 * For further information, refer to the getopt(3) man page.
 *
 * Return:
 * * An option character if an option is found. @gs.arg is set to the
 *   argument if there is one, otherwise it is set to ``NULL``.
 * * ``-1`` if there are no more options, or if an ``--`` argument is
 *   encountered.
 * * ``'?'`` if we encounter an option not in @optstring. @gs.opt is set to
 *   the unknown option.
 * * ``':'`` if an argument is required, but no argument follows the
 *   option. @gs.opt is set to the option missing its argument.
 *
 * @gs.index is always set to the index of the next unparsed argument in
 * @gs.argv.
 */
int getopt(struct getopt_state *gs, const char *optstring);

/**
 * getopt_silent() - Compatibility alias for getopt()
 * @gs: getopt state
 * @optstring: option specification, as for getopt()
 *
 * getopt() no longer prints error messages, so this is identical to
 * getopt(). Kept for callers (notably the unit tests) that named it
 * explicitly.
 *
 * Return: same as getopt()
 */
static inline int getopt_silent(struct getopt_state *gs, const char *optstring)
{
	return getopt(gs, optstring);
}

/**
 * getopt_pop() - Take the next remaining positional argument
 * @gs: State, after getopt() has returned -1
 *
 * Returns the first permuted non-option (``gs->argv[gs->index]``),
 * advances the index, and decrements ``gs->nonopts``. Returns NULL
 * when no positional arguments remain.
 *
 * Useful for consuming positionals one at a time after parsing::
 *
 *     algo = getopt_pop(&gs);
 *     return hash_command(algo, ..., gs.nonopts, &gs.argv[gs.index]);
 */
static inline char *getopt_pop(struct getopt_state *gs)
{
	if (gs->index >= gs->argc)
		return NULL;
#ifdef CONFIG_GETOPT_PERMUTE
	if (gs->nonopts > 0)
		gs->nonopts--;
#endif
	return gs->argv[gs->index++];
}

#endif /* __GETOPT_H */
