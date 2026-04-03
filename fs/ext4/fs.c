// SPDX-License-Identifier: GPL-2.0
/*
 * ext4 filesystem driver for the VFS layer
 *
 * Wraps the existing ext4 implementation to provide UCLASS_FS and
 * UCLASS_DIR devices, following the same pattern as the FAT VFS driver.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <blk.h>
#include <dir.h>
#include <dm.h>
#include <ext4fs.h>
#include <file.h>
#include <fs.h>
#include <iovec.h>
#include <malloc.h>
#include <vfs.h>
#include <dm/device-internal.h>

/**
 * struct ext4_dir_priv - Private info for ext4 directory devices
 *
 * @strm: Directory stream from ext4fs_opendir(), or NULL
 */
struct ext4_dir_priv {
	struct fs_dir_stream *strm;
};

static int ext4_vfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_plat *plat = dev_get_uclass_plat(dev);
	int ret;

	if (uc_priv->mounted)
		return log_msg_ret("emm", -EISCONN);

	if (!plat->desc)
		return log_msg_ret("emd", -ENODEV);

	ret = ext4fs_probe(plat->desc, &plat->part);
	if (ret)
		return log_msg_ret("emp", ret);

	uc_priv->mounted = true;

	return 0;
}

static int ext4_vfs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("euu", -ENOTCONN);

	ext4fs_close();
	uc_priv->mounted = false;

	return 0;
}

static int ext4_vfs_lookup_dir(struct udevice *dev, const char *path,
			       struct udevice **dirp)
{
	struct udevice *dir;
	int ret;

	ret = dir_add_probe(dev, DM_DRIVER_GET(ext4_vfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("eld", ret);

	*dirp = dir;

	return 0;
}

#if IS_ENABLED(CONFIG_EXT4_WRITE)
static int ext4_vfs_unlink(struct udevice *dev, const char *path)
{
	return ext4fs_filename_unlink((char *)path);
}

static int ext4_vfs_ln(struct udevice *dev, const char *path,
		       const char *target)
{
	return ext4fs_create_link(target, path);
}
#endif

static const struct fs_ops ext4_vfs_ops = {
	.mount		= ext4_vfs_mount,
	.unmount	= ext4_vfs_unmount,
	.lookup_dir	= ext4_vfs_lookup_dir,
#if IS_ENABLED(CONFIG_EXT4_WRITE)
	.unlink		= ext4_vfs_unlink,
	.ln		= ext4_vfs_ln,
#endif
};

U_BOOT_DRIVER(ext4_old_fs) = {
	.name	= "ext4_old_fs",
	.id	= UCLASS_FS,
	.ops	= &ext4_vfs_ops,
};

/* ext4 directory driver */

static int ext4_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct ext4_dir_priv *priv = dev_get_priv(dev);
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_dir_stream *ext4_strm;
	const char *path;
	int ret;

	path = *uc_priv->path ? uc_priv->path : "/";
	ret = ext4fs_opendir(path, &ext4_strm);
	if (ret)
		return log_msg_ret("edo", ret);

	priv->strm = ext4_strm;

	return 0;
}

static int ext4_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
			 struct fs_dirent *dent)
{
	struct ext4_dir_priv *priv = dev_get_priv(dev);
	struct fs_dirent *ext4_dent;
	int ret;

	ret = ext4fs_readdir(priv->strm, &ext4_dent);
	if (ret)
		return ret;

	*dent = *ext4_dent;

	return 0;
}

static int ext4_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct ext4_dir_priv *priv = dev_get_priv(dev);

	ext4fs_closedir(priv->strm);
	priv->strm = NULL;

	return 0;
}

/* ext4 file driver */

/**
 * struct ext4_file_priv - Private info for ext4 file devices
 *
 * @path: Full path within the ext4 filesystem
 */
struct ext4_file_priv {
	char path[FILE_MAX_PATH_LEN];
};

static ssize_t ext4_read_iter(struct udevice *dev, struct iov_iter *iter,
			      loff_t pos)
{
	struct ext4_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = ext4_read_file(priv->path, iter_iov_ptr(iter), pos,
			     iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("efr", ret);
	iter_advance(iter, actual);

	return actual;
}

#if IS_ENABLED(CONFIG_EXT4_WRITE)
static ssize_t ext4_write_iter(struct udevice *dev, struct iov_iter *iter,
			       loff_t pos)
{
	struct ext4_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = ext4_write_file(priv->path, (void *)iter_iov_ptr(iter), pos,
			      iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("efw", ret);
	iter_advance(iter, actual);

	return actual;
}
#endif

static struct file_ops ext4_file_ops = {
	.read_iter	= ext4_read_iter,
#if IS_ENABLED(CONFIG_EXT4_WRITE)
	.write_iter	= ext4_write_iter,
#endif
};

U_BOOT_DRIVER(ext4_vfs_file) = {
	.name		= "ext4_vfs_file",
	.id		= UCLASS_FILE,
	.ops		= &ext4_file_ops,
	.priv_auto	= sizeof(struct ext4_file_priv),
};

static int ext4_dir_open_file(struct udevice *dir, const char *leaf,
			      enum dir_open_flags_t oflags,
			      struct udevice **filp)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dir);
	struct ext4_file_priv *priv;
	struct udevice *dev;
	char path[FILE_MAX_PATH_LEN];
	loff_t size = 0;
	int ret;

	if (*uc_priv->path)
		snprintf(path, sizeof(path), "%s/%s", uc_priv->path, leaf);
	else
		snprintf(path, sizeof(path), "/%s", leaf);

	if (oflags == DIR_O_RDONLY) {
		if (!ext4fs_exists(path))
			return log_msg_ret("eoe", -ENOENT);
		ret = ext4fs_size(path, &size);
		if (ret)
			return log_msg_ret("eos", ret);
	}

	ret = file_add_probe(dir, DM_DRIVER_REF(ext4_vfs_file), leaf,
			     size, oflags, &dev);
	if (ret)
		return log_msg_ret("eop", ret);

	priv = dev_get_priv(dev);
	strlcpy(priv->path, path, sizeof(priv->path));
	*filp = dev;

	return 0;
}

static struct dir_ops ext4_dir_ops = {
	.open		= ext4_dir_open,
	.read		= ext4_dir_read,
	.close		= ext4_dir_close,
	.open_file	= ext4_dir_open_file,
};

U_BOOT_DRIVER(ext4_vfs_dir) = {
	.name		= "ext4_vfs_dir",
	.id		= UCLASS_DIR,
	.ops		= &ext4_dir_ops,
	.priv_auto	= sizeof(struct ext4_dir_priv),
};
