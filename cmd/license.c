// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2007 by OpenMoko, Inc.
 * Author: Harald Welte <laforge@openmoko.org>
 */

#include <command.h>
#include <getopt.h>
#include <gzip.h>
#include <malloc.h>

#include "license_data_gz.h"
#include "license_data_size.h"

static int do_license(struct getopt_state *gs)
{
	char *dst;
	unsigned long len = data_size;
	int ret = CMD_RET_SUCCESS;

	if (getopt(gs, "+") > 0)
		return CMD_RET_USAGE;

	dst = malloc(data_size + 1);
	if (!dst)
		return CMD_RET_FAILURE;

	ret = gunzip(dst, data_size, (unsigned char *)data_gz, &len);
	if (ret) {
		printf("Error uncompressing license text\n");
		ret = CMD_RET_FAILURE;
		goto free;
	}

	dst[data_size] = 0;
	puts(dst);

free:
	free(dst);

	return ret;
}

U_BOOT_CMD_GETOPT(
	license, 1, 1, do_license,
	"print GPL license text",
	""
);
