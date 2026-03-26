// SPDX-License-Identifier: GPL-2.0
/*
 * VFS-based filesystem commands - mount, umount, ls, load, save, size
 *
 * These replace the legacy commands in cmd/fs_legacy.c with versions that
 * use absolute paths through the virtual filesystem layer.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <blk.h>
#include <command.h>
#include <dm.h>
#include <env.h>
#include <file.h>
#include <fs.h>
#include <fs_legacy.h>
#include <mapmem.h>
#include <part.h>
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
	int part_num, ret;

	vfs = vfs_root();
	if (!vfs)
		return -ENXIO;

	if (argc < 2) {
		vfs_print_mounts();
		return 0;
	}

	/* mount -t <type> <interface> <dev:part> <mountpoint> */
	if (!strcmp(argv[1], "-t")) {
		struct disk_partition info;
		struct blk_desc *desc;

		if (argc < 6)
			return -EINVAL;

		ret = blk_get_device_part_str(argv[3], argv[4], &desc,
					      &info, 1);
		if (ret < 0)
			return ret;
		part_num = ret;

		return fs_mount_blkdev(argv[2], desc, part_num, &info,
				       argv[5]);
	}

	/* mount <iface> <dev:part> <mountpoint> - auto-detect type */
	if (argc == 4) {
		struct disk_partition info;
		struct blk_desc *desc;

		ret = blk_get_device_part_str(argv[1], argv[2], &desc,
					      &info, 1);
		if (ret < 0)
			return ret;
		part_num = ret;

		return fs_mount_blkdev_auto(desc, part_num, &info, argv[3]);
	}

	/* mount <dev> <mountpoint> - mount an existing UCLASS_FS device */
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
	mount,	6,	1,	do_mount,
	"mount a filesystem",
	"[<dev> <mountpoint>]\n"
	"    - With no args, list all mounts\n"
	"mount <iface> <dev:part> <mountpoint>\n"
	"    - Auto-detect and mount a filesystem\n"
	"mount -t <type> <iface> <dev:part> <mountpoint>\n"
	"    - Mount a specific filesystem type"
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

U_BOOT_CMD_COMPLETE(
	umount,	2,	1,	do_umount,
	"unmount a filesystem",
	"<mountpoint>\n"
	"    - Unmount the filesystem at 'mountpoint'",
	vfs_cmd_complete
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

U_BOOT_CMD_COMPLETE(
	cd,	2,	1,	do_cd,
	"change working directory",
	"[<path>]\n"
	"    - Change to 'path' in the VFS (default /)",
	vfs_cmd_complete
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

U_BOOT_CMD_COMPLETE(
	ls,	2,	1,	do_ls,
	"list files in a directory (default cwd)",
	"[<path>]\n"
	"    - List files at 'path' in the VFS (default cwd)",
	vfs_cmd_complete
);

#define COPY_BUF_SIZE	0x1000

int do_cp(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct file_uc_priv *uc_priv;
	struct udevice *src, *dst;
	char buf[COPY_BUF_SIZE];
	loff_t remaining, pos;
	long total = 0;
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	ret = vfs_open_file(argv[1], DIR_O_RDONLY, &src);
	if (ret) {
		printf("Source '%s' not found: %dE\n", argv[1], ret);
		return CMD_RET_FAILURE;
	}

	ret = vfs_open_file(argv[2], DIR_O_WRONLY, &dst);
	if (ret) {
		printf("Dest '%s' failed: %dE\n", argv[2], ret);
		return CMD_RET_FAILURE;
	}

	uc_priv = dev_get_uclass_priv(src);
	remaining = uc_priv->size;
	pos = 0;

	while (remaining > 0) {
		long chunk = min((loff_t)COPY_BUF_SIZE, remaining);
		long nread, nwritten;

		nread = file_read_at(src, buf, pos, chunk);
		if (nread < 0) {
			printf("Read failed: %ldE\n", nread);
			return CMD_RET_FAILURE;
		}
		if (!nread)
			break;

		nwritten = file_write_at(dst, buf, pos, nread);
		if (nwritten < 0) {
			printf("Write failed: %ldE\n", nwritten);
			return CMD_RET_FAILURE;
		}

		pos += nread;
		remaining -= nread;
		total += nwritten;
	}

	printf("%ld bytes copied\n", total);

	return CMD_RET_SUCCESS;
}

static const char *fs_type_name(unsigned int type)
{
	switch (type) {
	case FS_DT_DIR:
		return "directory";
	case FS_DT_REG:
		return "regular file";
	case FS_DT_LNK:
		return "symbolic link";
	default:
		return "unknown";
	}
}

static int do_stat(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	struct fs_dirent dent;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = vfs_stat(argv[1], &dent);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	printf("  File: %s\n", dent.name);
	printf("  Size: %llu\n", dent.size);
	printf("  Type: %s\n", fs_type_name(dent.type));
	if (dent.change_time.tm_year) {
		printf("Modify: %04d-%02d-%02d %02d:%02d:%02d\n",
		       dent.change_time.tm_year, dent.change_time.tm_mon,
		       dent.change_time.tm_mday, dent.change_time.tm_hour,
		       dent.change_time.tm_min, dent.change_time.tm_sec);
	}
	if (dent.access_time.tm_year) {
		printf("Access: %04d-%02d-%02d %02d:%02d:%02d\n",
		       dent.access_time.tm_year, dent.access_time.tm_mon,
		       dent.access_time.tm_mday, dent.access_time.tm_hour,
		       dent.access_time.tm_min, dent.access_time.tm_sec);
	}
	if (dent.create_time.tm_year) {
		printf(" Birth: %04d-%02d-%02d %02d:%02d:%02d\n",
		       dent.create_time.tm_year, dent.create_time.tm_mon,
		       dent.create_time.tm_mday, dent.create_time.tm_hour,
		       dent.create_time.tm_min, dent.create_time.tm_sec);
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	stat,	2,	1,	do_stat,
	"display file status",
	"<path>\n"
	"    - Show type, size and timestamps of a file or directory",
	vfs_cmd_complete
);

static int do_size(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	struct fs_dirent dent;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = vfs_stat(argv[1], &dent);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	env_set_hex("filesize", dent.size);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	size,	2,	0,	do_size,
	"determine a file's size",
	"<path>\n"
	"    - Find file at 'path' in the VFS, determine its size,\n"
	"      and store in the 'filesize' variable.",
	vfs_cmd_complete
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

U_BOOT_CMD_COMPLETE(
	load,	7,	0,	do_load_vfs,
	"load binary file from a filesystem",
	"<addr> <path> [bytes [pos]]\n"
	"    - Load binary file from 'path' in the VFS to address 'addr'.\n"
	"      'bytes' gives the size to load in bytes.\n"
	"      If 'bytes' is 0 or omitted, the file is read until the end.\n"
	"      'pos' gives the file byte position to start reading from.\n"
	"      If 'pos' is 0 or omitted, the file is read from the start.\n"
	"load <interface> [<dev[:part]> [<addr> [<filename> [bytes [pos]]]]]\n"
	"    - Legacy: load from block device interface",
	vfs_cmd_complete
);

static int do_save(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	struct udevice *fil;
	long bytes, written;
	unsigned long addr;
	loff_t pos = 0;
	void *buf;
	int ret;

	if (argc < 4)
		return CMD_RET_USAGE;

	addr = hextoul(argv[1], NULL);
	bytes = hextoul(argv[3], NULL);
	if (argc >= 5)
		pos = hextoul(argv[4], NULL);

	ret = vfs_open_file(argv[2], DIR_O_WRONLY, &fil);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	buf = map_sysmem(addr, bytes);
	written = file_write_at(fil, buf, pos, bytes);
	unmap_sysmem(buf);

	if (written < 0) {
		printf("Write failed: %ldE\n", written);
		return CMD_RET_FAILURE;
	}

	printf("%ld bytes written\n", written);

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	save,	5,	0,	do_save,
	"save memory to a file",
	"<addr> <path> <bytes> [pos]\n"
	"    - Save 'bytes' from address 'addr' to 'path' in the VFS.\n"
	"      'pos' gives the file byte position to start writing to.\n"
	"      If 'pos' is 0 or omitted, the file is written from the start.",
	vfs_cmd_complete
);
