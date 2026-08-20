// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2013 Patrice Bouchand <pbfwdlist_gmail_com>
 * lzma uncompress command in Uboot
 *
 * made from existing cmd_unzip.c file of Uboot
 *
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#include <command.h>
#include <env.h>
#include <getopt.h>
#include <mapmem.h>
#include <vsprintf.h>
#include <asm/io.h>

#include <lzma/LzmaTools.h>

static int do_lzmadec(struct getopt_state *gs)
{
	int argc = gs->argc;
	char *const *argv = gs->argv;
	unsigned long src, dst;
	SizeT src_len = ~0UL, dst_len = ~0UL;
	int ret;

	if (getopt(gs, "+") > 0)
		return CMD_RET_USAGE;

	switch (argc) {
	case 4:
		dst_len = hextoul(argv[3], NULL);
		/* fall through */
	case 3:
		src = hextoul(argv[1], NULL);
		dst = hextoul(argv[2], NULL);
		break;
	default:
		return CMD_RET_USAGE;
	}

	ret = lzmaBuffToBuffDecompress(map_sysmem(dst, dst_len), &dst_len,
				       map_sysmem(src, 0), src_len);

	if (ret != SZ_OK)
		return 1;
	printf("Uncompressed size: %ld = %#lX\n", (ulong)dst_len,
	       (ulong)dst_len);
	env_set_hex("filesize", dst_len);

	return 0;
}

U_BOOT_CMD_GETOPT(
	lzmadec,    4,    1,    do_lzmadec,
	"lzma uncompress a memory region",
	"srcaddr dstaddr [dstsize]"
);
