// SPDX-License-Identifier: GPL-2.0
/*
 * ext4l filesystem driver for the VFS layer
 *
 * Wraps the Linux-ported ext4 implementation (ext4l) to provide UCLASS_FS
 * and UCLASS_DIR devices, following the same pattern as sandboxfs.c.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <dir.h>
#include <dm.h>
#include <ext4l.h>
#include <file.h>
#include <fs.h>
#include <fs_legacy.h>
#include <iovec.h>
#include <malloc.h>
#include <vfs.h>
#include <dm/device-internal.h>

/**
 * struct ext4l_dir_priv - Private info for ext4l directory devices
 *
 * @strm: Directory stream from ext4l_opendir(), or NULL. Only one listing
 *	at a time is supported per directory device
 */
struct ext4l_dir_priv {
	struct fs_dir_stream *strm;
};

static int ext4l_vfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_plat *plat = dev_get_uclass_plat(dev);
	int ret;

	if (uc_priv->mounted)
		return log_msg_ret("emm", -EISCONN);

	if (!plat->desc)
		return log_msg_ret("emd", -ENODEV);

	ret = ext4l_probe(plat->desc, &plat->part);
	if (ret)
		return log_msg_ret("emp", ret);

	uc_priv->mounted = true;

	return 0;
}

static int ext4l_vfs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("euu", -ENOTCONN);

	ext4l_close();
	uc_priv->mounted = false;

	return 0;
}

static int ext4l_vfs_lookup_dir(struct udevice *dev, const char *path,
				struct udevice **dirp)
{
	struct udevice *dir;
	int ret;

	ret = dir_add_probe(dev, DM_DRIVER_GET(ext4l_vfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("eld", ret);

	*dirp = dir;

	return 0;
}

static int ext4l_vfs_ln(struct udevice *dev, const char *path,
			const char *target)
{
	return ext4l_ln(target, path);
}

static int ext4l_vfs_rename(struct udevice *dev, const char *old_path,
			    const char *new_path)
{
	return ext4l_rename(old_path, new_path);
}

static int ext4l_vfs_statfs(struct udevice *dev, struct fs_statfs *stats)
{
	return ext4l_statfs(stats);
}

static int ext4l_vfs_unlink(struct udevice *dev, const char *path)
{
	return ext4l_unlink(path);
}

static int ext4l_vfs_mkdir(struct udevice *dev, const char *path)
{
	return ext4l_mkdir(path);
}

static const struct fs_ops ext4l_vfs_ops = {
	.mount		= ext4l_vfs_mount,
	.unmount	= ext4l_vfs_unmount,
	.lookup_dir	= ext4l_vfs_lookup_dir,
	.ln		= ext4l_vfs_ln,
	.rename		= ext4l_vfs_rename,
	.statfs		= ext4l_vfs_statfs,
	.unlink		= ext4l_vfs_unlink,
	.mkdir		= ext4l_vfs_mkdir,
};

U_BOOT_DRIVER(ext4_fs) = {
	.name	= "ext4_fs",
	.id	= UCLASS_FS,
	.ops	= &ext4l_vfs_ops,
};

/* ext4l directory driver */

static int ext4l_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct ext4l_dir_priv *priv = dev_get_priv(dev);
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_dir_stream *ext4_strm;
	const char *path;
	int ret;

	path = *uc_priv->path ? uc_priv->path : "/";
	ret = ext4l_opendir(path, &ext4_strm);
	if (ret)
		return log_msg_ret("edo", ret);

	priv->strm = ext4_strm;

	return 0;
}

static int ext4l_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
			  struct fs_dirent *dent)
{
	struct ext4l_dir_priv *priv = dev_get_priv(dev);
	struct fs_dirent *ext4_dent;
	int ret;

	ret = ext4l_readdir(priv->strm, &ext4_dent);
	if (ret)
		return ret;

	*dent = *ext4_dent;

	return 0;
}

static int ext4l_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct ext4l_dir_priv *priv = dev_get_priv(dev);

	ext4l_closedir(priv->strm);
	priv->strm = NULL;

	return 0;
}

/* ext4l file driver - stores the full path for path-based I/O */

/**
 * struct ext4l_file_priv - Private info for ext4l file devices
 *
 * @path: Full path within the ext4 filesystem
 */
struct ext4l_file_priv {
	char path[FILE_MAX_PATH_LEN];
};

static ssize_t ext4l_read_iter(struct udevice *dev, struct iov_iter *iter,
			       loff_t pos)
{
	struct ext4l_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = ext4l_read(priv->path, iter_iov_ptr(iter), pos,
			 iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("efr", ret);
	iter_advance(iter, actual);

	return actual;
}

static ssize_t ext4l_write_iter(struct udevice *dev, struct iov_iter *iter,
				loff_t pos)
{
	struct ext4l_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = ext4l_write(priv->path, (void *)iter_iov_ptr(iter), pos,
			  iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("efw", ret);
	iter_advance(iter, actual);

	return actual;
}

static struct file_ops ext4l_file_ops = {
	.read_iter	= ext4l_read_iter,
	.write_iter	= ext4l_write_iter,
};

U_BOOT_DRIVER(ext4l_vfs_file) = {
	.name		= "ext4l_vfs_file",
	.id		= UCLASS_FILE,
	.ops		= &ext4l_file_ops,
	.priv_auto	= sizeof(struct ext4l_file_priv),
};

static int ext4l_dir_open_file(struct udevice *dir, const char *leaf,
			       enum dir_open_flags_t oflags,
			       struct udevice **filp)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dir);
	struct ext4l_file_priv *priv;
	struct udevice *dev;
	char path[FILE_MAX_PATH_LEN];
	loff_t size = 0;
	int ret;

	if (*uc_priv->path)
		snprintf(path, sizeof(path), "%s/%s", uc_priv->path, leaf);
	else
		snprintf(path, sizeof(path), "/%s", leaf);

	/* For read, verify the file exists and get its size */
	if (oflags == DIR_O_RDONLY) {
		if (!ext4l_exists(path))
			return log_msg_ret("eoe", -ENOENT);
		ret = ext4l_size(path, &size);
		if (ret)
			return log_msg_ret("eos", ret);
	}

	ret = file_add_probe(dir, DM_DRIVER_REF(ext4l_vfs_file), leaf,
			     size, oflags, &dev);
	if (ret)
		return log_msg_ret("eop", ret);

	priv = dev_get_priv(dev);
	strlcpy(priv->path, path, sizeof(priv->path));
	*filp = dev;

	return 0;
}

static struct dir_ops ext4l_dir_ops = {
	.open		= ext4l_dir_open,
	.read		= ext4l_dir_read,
	.close		= ext4l_dir_close,
	.open_file	= ext4l_dir_open_file,
};

U_BOOT_DRIVER(ext4l_vfs_dir) = {
	.name		= "ext4l_vfs_dir",
	.id		= UCLASS_DIR,
	.ops		= &ext4l_dir_ops,
	.priv_auto	= sizeof(struct ext4l_dir_priv),
};
