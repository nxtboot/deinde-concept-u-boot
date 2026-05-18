// SPDX-License-Identifier: GPL-2.0-only
/*
 * getopt.c - a simple getopt(3) implementation. See getopt.h for explanation.
 *
 * Copyright (C) 2020 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2007 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 */

#define LOG_CATEGORY LOGC_CORE

#include <getopt.h>
#include <log.h>
#include <linux/kernel.h>
#include <linux/string.h>

#ifdef CONFIG_GETOPT_PERMUTE
#define NONOPTS(gs)	((gs)->nonopts)
#else
#define NONOPTS(gs)	0
#endif

void getopt_init_state(struct getopt_state *gs, int argc, char *const argv[])
{
#ifdef CONFIG_GETOPT_PERMUTE
	int max = ARRAY_SIZE(gs->argv) - 1;

	if (argc > max)
		argc = max;

	memcpy(gs->argv, argv, (argc + 1) * sizeof(*gs->argv));
	gs->argv[argc] = NULL;
	gs->nonopts = 0;
#else
	/* POSIX mode never reorders, so borrow the caller's argv */
	gs->argv = argv;
#endif
	gs->argc = argc;
	gs->index = 1;
	gs->arg_index = 1;
}

int getopt(struct getopt_state *gs, const char *optstring)
{
	char curopt;	/* current option character */
	const char *curoptp;	/* pointer to the current option in optstring */
	int argc = gs->argc;
#ifdef CONFIG_GETOPT_PERMUTE
	char **argv = gs->argv;
	bool stop_nonopt = false;
#else
	char *const *argv = gs->argv;
#endif

	if (*optstring == '+') {
#ifdef CONFIG_GETOPT_PERMUTE
		stop_nonopt = true;
#endif
		optstring++;
	}

	while (1) {
		log_content("arg_index: %d index: %d nonopts: %d\n",
			    gs->arg_index, gs->index, NONOPTS(gs));

		/* `--` indicates the end of options */
		if (gs->arg_index == 1 && gs->index < argc &&
		    !strcmp(argv[gs->index], "--")) {
			gs->index++;
			return -1;
		}

#ifdef CONFIG_GETOPT_PERMUTE
		/*
		 * Permute non-options to the end so we can keep scanning
		 * for options past them. In '+' mode (POSIX), stop at the
		 * first non-option instead.
		 */
		while (gs->arg_index == 1 &&
		       gs->index + gs->nonopts < argc) {
			char *cur = argv[gs->index];
			int i;

			if (*cur == '-')
				break;
			if (stop_nonopt)
				return -1;

			gs->nonopts++;
			for (i = gs->index; i + 1 < argc; i++)
				argv[i] = argv[i + 1];
			argv[argc - 1] = cur;
		}
#else
		/* POSIX mode: stop at the first non-option */
		if (gs->arg_index == 1 && gs->index < argc &&
		    *argv[gs->index] != '-')
			return -1;
#endif

		/* Out of options to scan */
		if (gs->index + NONOPTS(gs) >= argc)
			return -1;

		/* We have found an option */
		curopt = argv[gs->index][gs->arg_index];
		if (curopt)
			break;
		/*
		 * No more options in current argv[] element; advance to the
		 * next one
		 */
		gs->index++;
		gs->arg_index = 1;
	}

	/* look up current option in optstring */
	curoptp = strchr(optstring, curopt);

	if (!curoptp) {
		gs->opt = curopt;
		gs->arg_index++;
		return '?';
	}

	if (*(curoptp + 1) != ':') {
		/* option with no argument. Just return it */
		gs->arg = NULL;
		gs->arg_index++;
		return curopt;
	}

	if (*(curoptp + 1) && *(curoptp + 2) == ':') {
		/* optional argument */
		if (argv[gs->index][gs->arg_index + 1]) {
			/* optional argument with directly following arg */
			gs->arg = argv[gs->index++] + gs->arg_index + 1;
			gs->arg_index = 1;
			return curopt;
		}
		if (gs->index + NONOPTS(gs) + 1 == argc) {
			/* We are at the last argv[] element */
			gs->arg = NULL;
			gs->index++;
			return curopt;
		}
		if (*argv[gs->index + 1] != '-') {
			/*
			 * optional argument with arg in next argv[] element
			 */
			gs->index++;
			gs->arg = argv[gs->index++];
			gs->arg_index = 1;
			return curopt;
		}

		/* no optional argument found */
		gs->arg = NULL;
		gs->arg_index = 1;
		gs->index++;
		return curopt;
	}

	if (argv[gs->index][gs->arg_index + 1]) {
		/* required argument with directly following arg */
		gs->arg = argv[gs->index++] + gs->arg_index + 1;
		gs->arg_index = 1;
		return curopt;
	}

	gs->index++;
	gs->arg_index = 1;

	if (gs->index + NONOPTS(gs) >= argc || argv[gs->index][0] == '-') {
		gs->opt = curopt;
		return ':';
	}

	gs->arg = argv[gs->index++];
	return curopt;
}
