// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2000-2009
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#include <command.h>
#include <getopt.h>

static int do_echo(struct getopt_state *gs)
{
	bool space = false;
	bool newline = true;
	const char *arg;
	int opt;

	while ((opt = getopt(gs, "+n")) > 0) {
		switch (opt) {
		case 'n':
			newline = false;
			break;
		default:
			return CMD_RET_USAGE;
		}
	}

	while ((arg = getopt_pop(gs))) {
		if (space)
			putc(' ');
		puts(arg);
		space = true;
	}

	if (newline)
		putc('\n');

	return 0;
}

U_BOOT_CMD_GETOPT(
	echo, CONFIG_SYS_MAXARGS, 1, do_echo,
	"echo args to console",
	"[-n] [args..]\n"
	"    - echo args to console; -n suppresses newline"
);
