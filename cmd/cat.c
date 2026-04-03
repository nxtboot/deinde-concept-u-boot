// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022
 * Roger Knecht <rknecht@pm.de>
 */

#include <abuf.h>
#include <command.h>
#include <dm.h>
#include <file.h>
#include <fs_legacy.h>
#include <malloc.h>
#include <mapmem.h>
#include <vfs.h>
#include <linux/errno.h>

#if IS_ENABLED(CONFIG_VFS)

#define CAT_BUF_SIZE	0x1000

static int do_cat(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[])
{
	struct file_uc_priv *uc_priv;
	struct udevice *fil;
	char buf[CAT_BUF_SIZE];
	loff_t remaining;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = vfs_open_file(argv[1], DIR_O_RDONLY, &fil);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	uc_priv = dev_get_uclass_priv(fil);
	remaining = uc_priv->size;

	while (remaining > 0) {
		long chunk = min((loff_t)CAT_BUF_SIZE - 1, remaining);
		long nread;

		nread = file_read(fil, buf, chunk);
		if (nread < 0) {
			printf("Read failed: %ldE\n", nread);
			return CMD_RET_FAILURE;
		}
		if (!nread)
			break;

		buf[nread] = '\0';
		puts(buf);
		remaining -= nread;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_LONGHELP(cat,
	"<path>\n"
	"    - Print the contents of a file in the VFS");

U_BOOT_CMD_COMPLETE(
	cat,	2,	1,	do_cat,
	"print file to standard output",
	cat_help_text,
	vfs_cmd_complete
);

#else /* !CONFIG_VFS */

static int do_cat(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[])
{
	struct abuf buf;
	char *ifname;
	char *dev;
	char *file;
	int ret;

	if (argc < 4)
		return CMD_RET_USAGE;

	ifname = argv[1];
	dev = argv[2];
	file = argv[3];

	ret = fs_load_alloc(ifname, dev, file, 0, 0, &buf);

	/* check file exists */
	switch (ret) {
	case 0:
		break;
	case -ENOMEDIUM:
		return CMD_RET_FAILURE;
	case -ENOENT:
		log_err("File does not exist: ifname=%s dev=%s file=%s\n",
			ifname, dev, file);
		return CMD_RET_FAILURE;
	case -E2BIG:
		log_err("File is too large: ifname=%s dev=%s file=%s\n",
			ifname, dev, file);
		return CMD_RET_FAILURE;
	case -ENOMEM:
		log_err("Not enough memory: ifname=%s dev=%s file=%s\n",
			ifname, dev, file);
		return CMD_RET_FAILURE;
	default:
	case -EIO:
		log_err("File-read failed: ifname=%s dev=%s file=%s\n",
			ifname, dev, file);
		return CMD_RET_FAILURE;
	}

	/* print file content */
	((char *)buf.data)[buf.size] = '\0';
	puts(buf.data);

	abuf_uninit(&buf);

	return 0;
}

U_BOOT_LONGHELP(cat,
	"<interface> <dev[:part]> <file>\n"
	"  - Print file from 'dev' on 'interface' to standard output\n");

U_BOOT_CMD(cat, 4, 1, do_cat,
	   "Print file to standard output",
	   cat_help_text
);
#endif /* CONFIG_VFS */
