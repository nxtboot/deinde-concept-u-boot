// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2012 The Chromium OS Authors.
 *
 * (C) Copyright 2011
 * Joe Hershberger, National Instruments, joe.hershberger@ni.com
 *
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#include <command.h>
#include <env.h>
#include <getopt.h>
#include <hash.h>
#include <linux/string.h>

#if IS_ENABLED(CONFIG_HASH_VERIFY)
#define HARGS 6
#else
#define HARGS 5
#endif

static int do_hash(struct getopt_state *gs)
{
	const char *optstring = IS_ENABLED(CONFIG_HASH_VERIFY) ? "+v" : "+";
	int flags = HASH_FLAG_ENV;
	char *algo;
	int opt;

	while ((opt = getopt(gs, optstring)) > 0) {
		switch (opt) {
		case 'v':
			flags |= HASH_FLAG_VERIFY;
			break;
		default:
			return CMD_RET_USAGE;
		}
	}

	/* Need at least: algorithm address count */
	if (gs->argc - gs->index < 3)
		return CMD_RET_USAGE;

	algo = strlower(getopt_pop(gs));

	return hash_command(algo, flags, gs->argc - gs->index,
			    &gs->argv[gs->index]);
}

U_BOOT_CMD_GETOPT(
	hash,	HARGS,	1,	do_hash,
	"compute hash message digest",
	"algorithm address count [[*]hash_dest]\n"
		"    - compute message digest [save to env var / *address]"
#if IS_ENABLED(CONFIG_HASH_VERIFY)
	"\nhash -v algorithm address count [*]hash\n"
		"    - verify message digest of memory area to immediate value, \n"
		"      env var or *address"
#endif
);
