// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020
 * FUJITSU COMPUTERTECHNOLOGIES LIMITED. All rights reserved.
 */

#include <command.h>
#include <env.h>
#include <getopt.h>
#include <mapmem.h>
#include <vsprintf.h>
#include <u-boot/lz4.h>

static int do_unlz4(struct getopt_state *gs)
{
	int argc = gs->argc;
	char *const *argv = gs->argv;
	unsigned long src, dst;
	size_t src_len = ~0UL, dst_len = ~0UL;
	int ret;

	if (getopt(gs, "+") > 0)
		return CMD_RET_USAGE;

	switch (argc) {
	case 4:
		src = hextoul(argv[1], NULL);
		dst = hextoul(argv[2], NULL);
		dst_len = hextoul(argv[3], NULL);
		break;
	default:
		return CMD_RET_USAGE;
	}

	ret = ulz4fn(map_sysmem(src, 0), src_len, map_sysmem(dst, dst_len),
		     &dst_len);
	if (ret) {
		printf("Uncompressed err :%d\n", ret);
		return 1;
	}

	printf("Uncompressed size: %zd = 0x%zX\n", dst_len, dst_len);
	env_set_hex("filesize", dst_len);

	return 0;
}

U_BOOT_CMD_GETOPT(unlz4, 4, 1, do_unlz4,
		  "lz4 uncompress a memory region",
		  "srcaddr dstaddr dstsize\n"
		  "NOTE: Specify the destination size that is sufficiently larger\n"
		  " than the source size.\n"
);
