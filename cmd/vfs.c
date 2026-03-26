// SPDX-License-Identifier: GPL-2.0
/*
 * VFS commands - 'fs mount', 'fs umount', 'fs ls'
 *
 * Provides a new 'fs' command with subcommands for the virtual filesystem
 * layer, co-existing with the legacy filesystem commands in cmd/fs.c.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <fs.h>
#include <vfs.h>
#include <dm/uclass.h>

static int fs_mount_handler(int argc, char *const argv[])
{
	struct udevice *vfs, *fsdev, *dir;
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

	ret = uclass_get_device_by_name(UCLASS_FS, argv[1], &fsdev);
	if (ret)
		return ret;

	ret = vfs_resolve(vfs, argv[2], &dir);
	if (ret)
		return ret;

	return vfs_mount(vfs, dir, fsdev);
}

static int do_fs_mount(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	int ret;

	if (argc == 2)
		return CMD_RET_USAGE;

	ret = fs_mount_handler(argc, argv);
	if (ret) {
		printf("fs mount failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

static int fs_umount_handler(const char *path)
{
	struct udevice *vfs;

	vfs = vfs_root();
	if (!vfs)
		return -ENXIO;

	return vfs_umount_path(vfs, path);
}

static int do_fs_umount(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = fs_umount_handler(argv[1]);
	if (ret) {
		printf("fs umount failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

static int do_fs_ls(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	const char *path = argc >= 2 ? argv[1] : "/";
	int ret;

	ret = vfs_ls(path);
	if (ret) {
		printf("fs ls failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_LONGHELP(fs,
	"mount [<dev> <mountpoint>]                   - list or create mounts\n"
	"fs mount <iface> <dev:part> <path>             - auto-detect and mount\n"
	"fs mount -t <type> <iface> <dev:part> <path>  - mount specific type\n"
	"fs umount <mountpoint>                        - unmount a filesystem\n"
	"fs ls [<path>]                                - list directory (default /)\n"
	"fs cp <source> <dest>                         - copy a file");

U_BOOT_CMD_WITH_SUBCMDS(fs, "Filesystem operations", fs_help_text,
	U_BOOT_SUBCMD_MKENT(mount, 6, 1, do_fs_mount),
	U_BOOT_SUBCMD_MKENT(umount, 2, 1, do_fs_umount),
	U_BOOT_SUBCMD_MKENT(ls, 2, 1, do_fs_ls),
	U_BOOT_SUBCMD_MKENT(cp, 3, 0, do_cp));
