// SPDX-License-Identifier: GPL-2.0+
/*
 * cmd_fs_uuid.c -- fsuuid command
 *
 * Copyright (C) 2014, Bachmann electronic GmbH
 */

#include <command.h>
#include <fs_cmd.h>
#include <fs_legacy.h>
#include <getopt.h>

static int do_fs_uuid_wrapper(struct getopt_state *gs)
{
	if (getopt(gs, "+") > 0)
		return CMD_RET_USAGE;

	return do_fs_uuid(gs->argc, gs->argv, FS_TYPE_ANY);
}

U_BOOT_CMD_GETOPT(
	fsuuid, 4, 1, do_fs_uuid_wrapper,
	"Look up a filesystem UUID",
	"<interface> <dev>:<part>\n"
	"    - print filesystem UUID\n"
	"fsuuid <interface> <dev>:<part> <varname>\n"
	"    - set environment variable to filesystem UUID\n"
);
