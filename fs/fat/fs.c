// SPDX-License-Identifier: GPL-2.0
/*
 * FAT filesystem driver for the VFS layer
 *
 * Wraps the existing FAT implementation to provide UCLASS_FS and
 * UCLASS_DIR devices, following the same pattern as sandboxfs.c.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <dir.h>
#include <dm.h>
#include <fat.h>
#include <file.h>
#include <fs.h>
#include <fs_legacy.h>
#include <iovec.h>
#include <malloc.h>
#include <vfs.h>
#include <dm/device-internal.h>

/**
 * struct fat_dir_priv - Private info for FAT directory devices
 *
 * @strm: Directory stream from fat_opendir(), or NULL
 */
struct fat_dir_priv {
	struct fs_dir_stream *strm;
};

static int fat_vfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_plat *plat = dev_get_uclass_plat(dev);

	if (uc_priv->mounted)
		return log_msg_ret("fmm", -EISCONN);

	if (!plat->desc)
		return log_msg_ret("fmd", -ENODEV);

	if (fat_set_blk_dev(plat->desc, &plat->part))
		return log_msg_ret("fmf", -EINVAL);

	uc_priv->mounted = true;

	return 0;
}

static int fat_vfs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("fuu", -ENOTCONN);

	fat_close();
	uc_priv->mounted = false;

	return 0;
}

static int fat_vfs_lookup_dir(struct udevice *dev, const char *path,
			      struct udevice **dirp)
{
	struct udevice *dir;
	int ret;

	ret = dir_add_probe(dev, DM_DRIVER_GET(fat_vfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("fld", ret);

	*dirp = dir;

	return 0;
}

static int fat_vfs_mkdir(struct udevice *dev, const char *path)
{
	return fat_mkdir(path);
}

static int fat_vfs_unlink(struct udevice *dev, const char *path)
{
	return fat_unlink(path);
}

static int fat_vfs_rename(struct udevice *dev, const char *old_path,
			  const char *new_path)
{
	return fat_rename(old_path, new_path);
}

static int fat_vfs_statfs(struct udevice *dev, struct fs_statfs *stats)
{
	return fat_statfs(stats);
}

static const struct fs_ops fat_vfs_ops = {
	.mount		= fat_vfs_mount,
	.unmount	= fat_vfs_unmount,
	.lookup_dir	= fat_vfs_lookup_dir,
	.mkdir		= fat_vfs_mkdir,
	.unlink		= fat_vfs_unlink,
	.rename		= fat_vfs_rename,
	.statfs		= fat_vfs_statfs,
};

U_BOOT_DRIVER(fat_fs) = {
	.name	= "fat_fs",
	.id	= UCLASS_FS,
	.ops	= &fat_vfs_ops,
};

/* FAT directory driver */

static int fat_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct fat_dir_priv *priv = dev_get_priv(dev);
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_dir_stream *fat_strm;
	const char *path;
	int ret;

	path = *uc_priv->path ? uc_priv->path : "/";
	ret = fat_opendir(path, &fat_strm);
	if (ret)
		return log_msg_ret("fdo", ret);

	priv->strm = fat_strm;

	return 0;
}

static int fat_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
			struct fs_dirent *dent)
{
	struct fat_dir_priv *priv = dev_get_priv(dev);
	struct fs_dirent *fat_dent;
	int ret;

	ret = fat_readdir(priv->strm, &fat_dent);
	if (ret)
		return ret;

	memcpy(dent, fat_dent, sizeof(*dent));

	return 0;
}

static int fat_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct fat_dir_priv *priv = dev_get_priv(dev);

	fat_closedir(priv->strm);
	priv->strm = NULL;

	return 0;
}

/* FAT file driver - stores the full path for path-based I/O */

/**
 * struct fat_file_priv - Private info for FAT file devices
 *
 * @path: Full path within the FAT filesystem
 */
struct fat_file_priv {
	char path[FILE_MAX_PATH_LEN];
};

static ssize_t fat_read_iter(struct udevice *dev, struct iov_iter *iter,
			     loff_t pos)
{
	struct fat_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = fat_read_file(priv->path, iter_iov_ptr(iter), pos,
			    iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("ffr", ret);
	iter_advance(iter, actual);

	return actual;
}

static ssize_t fat_write_iter(struct udevice *dev, struct iov_iter *iter,
			      loff_t pos)
{
	struct fat_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = file_fat_write(priv->path, (void *)iter_iov_ptr(iter), pos,
			     iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("ffw", ret);
	iter_advance(iter, actual);

	return actual;
}

static struct file_ops fat_file_ops = {
	.read_iter	= fat_read_iter,
	.write_iter	= fat_write_iter,
};

U_BOOT_DRIVER(fat_vfs_file) = {
	.name		= "fat_vfs_file",
	.id		= UCLASS_FILE,
	.ops		= &fat_file_ops,
	.priv_auto	= sizeof(struct fat_file_priv),
};

static int fat_dir_open_file(struct udevice *dir, const char *leaf,
			     enum dir_open_flags_t oflags,
			     struct udevice **filp)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dir);
	struct fat_file_priv *priv;
	struct udevice *dev;
	char path[FILE_MAX_PATH_LEN];
	loff_t size = 0;
	int ret;

	if (*uc_priv->path)
		snprintf(path, sizeof(path), "%s/%s", uc_priv->path, leaf);
	else
		snprintf(path, sizeof(path), "/%s", leaf);

	if (oflags == DIR_O_RDONLY) {
		if (!fat_exists(path))
			return log_msg_ret("foe", -ENOENT);
		ret = fat_size(path, &size);
		if (ret)
			return log_msg_ret("fos", ret);
	}

	ret = file_add_probe(dir, DM_DRIVER_REF(fat_vfs_file), leaf,
			     size, oflags, &dev);
	if (ret)
		return log_msg_ret("fop", ret);

	priv = dev_get_priv(dev);
	strlcpy(priv->path, path, sizeof(priv->path));
	*filp = dev;

	return 0;
}

static struct dir_ops fat_dir_ops = {
	.open		= fat_dir_open,
	.read		= fat_dir_read,
	.close		= fat_dir_close,
	.open_file	= fat_dir_open_file,
};

U_BOOT_DRIVER(fat_vfs_dir) = {
	.name		= "fat_vfs_dir",
	.id		= UCLASS_DIR,
	.ops		= &fat_dir_ops,
	.priv_auto	= sizeof(struct fat_dir_priv),
};
