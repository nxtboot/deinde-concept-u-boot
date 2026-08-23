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
#include <malloc.h>
#include <mapmem.h>
#include <part.h>
#include <vfs.h>
#include <dm/uclass.h>

int do_load(int argc, char *const argv[], int fstype);
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

	/* mount -a: process entries from 'fstab' env variable */
	if (!strcmp(argv[1], "-a")) {
		const char *fstab = env_get("fstab");
		char *buf, *entry, *next;

		if (!fstab) {
			printf("No 'fstab' environment variable set\n");
			return CMD_RET_FAILURE;
		}

		buf = strdup(fstab);
		if (!buf)
			return CMD_RET_FAILURE;

		for (entry = buf; entry; entry = next) {
			next = strchr(entry, ';');
			if (next)
				*next++ = '\0';

			/* Skip leading whitespace */
			while (*entry == ' ')
				entry++;
			if (!*entry)
				continue;

			run_commandf("mount %s", entry);
		}

		free(buf);
		return CMD_RET_SUCCESS;
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

	if (!strcmp(argv[1], "-a"))
		ret = vfs_umount_all();
	else
		ret = umount_handler(argv[1]);
	if (ret) {
		printf("umount failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	umount,	2,	1,	do_umount,
	"unmount a filesystem",
	"<mountpoint> | -a\n"
	"    - Unmount the filesystem at 'mountpoint', or all (-a)",
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

static int do_rm(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = vfs_unlink(argv[1]);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	rm,	2,	0,	do_rm,
	"delete a file",
	"<path>\n"
	"    - Delete the file at 'path' in the VFS",
	vfs_cmd_complete
);

static int do_mv(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	ret = vfs_rename(argv[1], argv[2]);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	mv,	3,	0,	do_mv,
	"rename/move a file or directory",
	"<old_path> <new_path>\n"
	"    - Rename or move a file/directory within the same filesystem",
	vfs_cmd_complete
);

static int do_mkdir(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = vfs_mkdir(argv[1]);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	mkdir,	2,	0,	do_mkdir,
	"create a directory",
	"<path>\n"
	"    - Create directory at 'path' in the VFS",
	vfs_cmd_complete
);

static int do_ln(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	int ret;

	if (argc < 3)
		return CMD_RET_USAGE;

	ret = vfs_ln(argv[2], argv[1]);
	if (ret) {
		printf("Error: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	ln,	3,	0,	do_ln,
	"create a symbolic link",
	"<target> <linkname>\n"
	"    - Create symlink 'linkname' pointing to 'target'",
	vfs_cmd_complete
);

static int do_tree_recurse(const char *path, int depth, int max_depth)
{
	struct udevice *mnt_dev, *dir;
	struct fs_dir_stream *strm;
	const char *subpath;
	struct fs_dirent dent;
	struct vfsmount *mnt;
	int ret;

	if (max_depth && depth >= max_depth)
		return 0;

	if (!strcmp(path, "/")) {
		/* List root mount points */
		struct udevice *dev;

		uclass_foreach_dev_probe(UCLASS_MOUNT, dev) {
			struct dir_uc_priv *uc_priv;
			char mpath[FILE_MAX_PATH_LEN];

			mnt = dev_get_uclass_priv(dev);
			uc_priv = dev_get_uclass_priv(mnt->dir);
			printf("%*s%s/\n", depth * 4, "", uc_priv->path);
			snprintf(mpath, sizeof(mpath), "/%s", uc_priv->path);
			do_tree_recurse(mpath, depth + 1, max_depth);
		}
		return 0;
	}

	{
		struct udevice *vfs;

		vfs = vfs_root();
		if (!vfs)
			return -ENXIO;
		ret = vfs_find_mount(vfs, path, &mnt_dev, &subpath);
	}
	if (ret)
		return ret;

	mnt = dev_get_uclass_priv(mnt_dev);
	ret = fs_lookup_dir(mnt->target, subpath, &dir);
	if (ret)
		return ret;

	ret = dir_open(dir, &strm);
	if (ret)
		return ret;

	while (!dir_read(dir, strm, &dent)) {
		if (!strcmp(dent.name, ".") || !strcmp(dent.name, ".."))
			continue;

		printf("%*s%s%s\n", depth * 4, "", dent.name,
		       dent.type == FS_DT_DIR ? "/" :
		       dent.type == FS_DT_LNK ? "@" : "");

		if (dent.type == FS_DT_DIR) {
			char child_path[FILE_MAX_PATH_LEN];

			if (path[strlen(path) - 1] == '/')
				snprintf(child_path, sizeof(child_path),
					 "%s%s", path, dent.name);
			else
				snprintf(child_path, sizeof(child_path),
					 "%s/%s", path, dent.name);
			do_tree_recurse(child_path, depth + 1, max_depth);
		}
	}

	dir_close(dir, strm);

	return 0;
}

static int do_tree(struct cmd_tbl *cmdtp, int flag, int argc,
		   char *const argv[])
{
	const char *path = argc >= 2 ? argv[1] : NULL;
	char resolved[FILE_MAX_PATH_LEN];
	int ret;

	ret = vfs_init();
	if (ret)
		return CMD_RET_FAILURE;

	path = path ? path : vfs_getcwd();
	if (path[0] != '/') {
		const char *cwd = vfs_getcwd();
		int cwd_len = strlen(cwd);

		if (cwd_len == 1)
			snprintf(resolved, sizeof(resolved), "/%s", path);
		else
			snprintf(resolved, sizeof(resolved), "%s/%s", cwd,
				 path);
		path = resolved;
	}

	printf("%s\n", path);
	ret = do_tree_recurse(path, 1, 3);
	if (ret) {
		printf("tree failed: %dE\n", ret);
		return CMD_RET_FAILURE;
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD_COMPLETE(
	tree,	2,	1,	do_tree,
	"list directory tree",
	"[<path>]\n"
	"    - Recursively list directory tree (max 3 levels deep)",
	vfs_cmd_complete
);

static void print_size_human(u64 bytes)
{
	if (bytes >= (u64)1024 * 1024 * 1024)
		printf("%llu.%llu GiB", bytes >> 30,
		       (bytes & ((1 << 30) - 1)) / (((u64)1 << 30) / 10));
	else if (bytes >= 1024 * 1024)
		printf("%llu.%llu MiB", bytes >> 20,
		       (bytes & ((1 << 20) - 1)) / ((1 << 20) / 10));
	else if (bytes >= 1024)
		printf("%llu KiB", bytes >> 10);
	else
		printf("%llu B", bytes);
}

static void print_df_one(const char *name, struct fs_statfs *stats)
{
	u64 used = stats->blocks - stats->bfree;
	u64 total_bytes = stats->blocks * stats->bsize;
	u64 free_bytes = stats->bfree * stats->bsize;
	u64 used_bytes = used * stats->bsize;

	printf("%-16s %6lu %10llu %10llu %10llu  ", name, stats->bsize,
	       total_bytes, used_bytes, free_bytes);
	print_size_human(total_bytes);
	printf("\n");
}

static int do_df(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct fs_statfs stats;
	int ret;

	if (argc >= 2) {
		/* Single filesystem */
		ret = vfs_statfs(argv[1], &stats);
		if (ret) {
			printf("Error: %dE\n", ret);
			return CMD_RET_FAILURE;
		}
		printf("%-16s %6s %10s %10s %10s  %s\n",
		       "Filesystem", "Blksz", "Total", "Used", "Free",
		       "Size");
		print_df_one(argv[1], &stats);
	} else {
		/* All mounts */
		ret = vfs_init();
		if (ret)
			return CMD_RET_FAILURE;

		printf("%-16s %6s %10s %10s %10s  %s\n",
		       "Filesystem", "Blksz", "Total", "Used", "Free",
		       "Size");
		vfs_print_df();
	}

	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
	df,	2,	1,	do_df,
	"show filesystem usage",
	"[<path>]\n"
	"    - Show usage for the filesystem at 'path', or all mounts"
);

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
	if (dent.type == FS_DT_LNK) {
		char target[FILE_MAX_PATH_LEN];
		int len;

		len = vfs_readlink(argv[1], target, sizeof(target));
		if (len >= 0)
			printf("  Link: %s\n", target);
	}
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
			return do_load(argc, argv, FS_TYPE_ANY);
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
		printf("Error: %dE\n", ret);
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
