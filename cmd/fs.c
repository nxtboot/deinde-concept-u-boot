// SPDX-License-Identifier: GPL-2.0
/*
 * VFS-based filesystem commands - mount, umount, ls, load
 *
 * These replace the legacy commands in cmd/fs_legacy.c with versions that
 * use absolute paths through the virtual filesystem layer.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <env.h>
#include <file.h>
#include <fs_legacy.h>
#include <mapmem.h>
#include <vfs.h>
#include <dm/uclass.h>

int do_load(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[],
	    int fstype);
int do_fs_types(struct cmd_tbl *cmdtp, int flag, int argc,
		char *const argv[]);

static int mount_handler(int argc, char *const argv[])
{
	struct udevice *vfs, *fsdev, *dir, *mnt;
	const char *subpath;
	int ret;

	vfs = vfs_root();
	if (!vfs)
		return -ENXIO;

	if (argc < 2) {
		vfs_print_mounts();
		return 0;
	}

	if (argc < 3)
		return -EINVAL;

	/* Check if already mounted */
	ret = vfs_find_mount(vfs, argv[2], &mnt, &subpath);
	if (!ret && mnt && !*subpath)
		return -EBUSY;

	ret = uclass_get_device_by_name(UCLASS_FS, argv[1], &fsdev);
	if (ret)
		return ret;

	ret = vfs_resolve(vfs, argv[2], &dir);
	if (ret)
		return ret;

	return vfs_mount(vfs, dir, fsdev);
}

static int do_mount(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	int ret;

	ret = mount_handler(argc, argv);
	if (ret) {
		printf("mount failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	mount,	3,	1,	do_mount,
	"mount a filesystem",
	"[<dev> <mountpoint>]\n"
	"    - With no args, list all mounts\n"
	"    - Mount device 'dev' at 'mountpoint'"
);

static int umount_handler(const char *path)
{
	struct udevice *vfs;

	vfs = vfs_root();
	if (!vfs)
		return -ENXIO;

	return vfs_umount_path(vfs, path);
}

static int do_umount(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = umount_handler(argv[1]);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	umount,	2,	1,	do_umount,
	"unmount a filesystem",
	"<mountpoint>\n"
	"    - Unmount the filesystem at 'mountpoint'"
);

static int do_cd(struct cmd_tbl *cmdtp, int flag, int argc,
		 char *const argv[])
{
	const char *path = argc >= 2 ? argv[1] : "/";
	int ret;

	ret = vfs_chdir(path);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	cd,	2,	1,	do_cd,
	"change working directory",
	"[<path>]\n"
	"    - Change to 'path' in the VFS (default /)"
);

static int do_pwd(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[])
{
	printf("%s\n", vfs_getcwd());

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	pwd,	1,	1,	do_pwd,
	"print working directory",
	"\n    - Print the current VFS working directory"
);

static int do_ls(struct cmd_tbl *cmdtp, int flag, int argc,
		 char *const argv[])
{
	const char *path = argc >= 2 ? argv[1] : NULL;
	int ret;

	ret = vfs_ls(path);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	ls,	2,	1,	do_ls,
	"list files in a directory (default cwd)",
	"[<path>]\n"
	"    - List files at 'path' in the VFS (default cwd)"
);


static int do_vfs_load(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	struct file_uc_priv *uc_priv;
	ulong addr, bytes = 0;
	struct udevice *fil;
	long len_read;
	loff_t pos = 0;
	void *buf;
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	addr = hextoul(argv[1], NULL);
	if (argc >= 4)
		bytes = hextoul(argv[3], NULL);
	if (argc >= 5)
		pos = hextoull(argv[4], NULL);

	ret = vfs_open_file(argv[2], DIR_O_RDONLY, &fil);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	uc_priv = dev_get_uclass_priv(fil);
	if (!bytes)
		bytes = uc_priv->size - pos;

	buf = map_sysmem(addr, bytes);
	len_read = file_read_at(fil, buf, pos, bytes);
	unmap_sysmem(buf);

	if (len_read < 0) {
		printf("Read failed: %ldE\n", len_read);
		return CMD_RET_FAILURE;
	}

	env_set_hex("fileaddr", addr);
	env_set_hex("filesize", len_read);

	printf("%ld bytes read\n", len_read);

	return CMD_RET_SUCCESS;
}

static int do_load_vfs(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	char *endp;

	/*
	 * Detect legacy syntax: load <interface> [<dev[:part]> ...]
	 * If argv[1] is not a pure hex number, assume legacy syntax.
	 */
	if (argc >= 2) {
		hextoul(argv[1], &endp);
		if (*endp)
			return do_load(cmdtp, flag, argc, argv, FS_TYPE_ANY);
	}

	return do_vfs_load(cmdtp, flag, argc, argv);
}

static int do_fstypes(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	return do_fs_types(cmdtp, flag, argc, argv);
}

U_BOOT_CMD(
	fstypes, 1, 1, do_fstypes,
	"List supported filesystem types", ""
);

U_BOOT_CMD(
	load,	7,	0,	do_load_vfs,
	"load binary file from a filesystem",
	"<addr> <path> [bytes [pos]]\n"
	"    - Load binary file from 'path' in the VFS to address 'addr'.\n"
	"      'bytes' gives the size to load in bytes.\n"
	"      If 'bytes' is 0 or omitted, the file is read until the end.\n"
	"      'pos' gives the file byte position to start reading from.\n"
	"      If 'pos' is 0 or omitted, the file is read from the start.\n"
	"load <interface> [<dev[:part]> [<addr> [<filename> [bytes [pos]]]]]\n"
	"    - Legacy: load from block device interface"
);
