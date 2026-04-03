// SPDX-License-Identifier: GPL-2.0
/*
 * isofs filesystem driver for the VFS layer
 *
 * Wraps the Linux-ported isofs implementation to provide UCLASS_FS
 * and UCLASS_DIR devices, following the same pattern as ext4l/fs.c.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
#include <fs_legacy.h>
#include <isofs.h>
#include <iovec.h>
#include <malloc.h>
#include <vfs.h>
#include <dm/device-internal.h>

/**
 * struct isofs_dir_priv - Private info for isofs directory devices
 *
 * @strm: Directory stream from isofs_opendir(), or NULL
 */
struct isofs_dir_priv {
	struct fs_dir_stream *strm;
};

static int isofs_vfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);
	struct fs_plat *plat = dev_get_uclass_plat(dev);
	int ret;

	if (uc_priv->mounted)
		return log_msg_ret("emm", -EISCONN);

	if (!plat->desc)
		return log_msg_ret("emd", -ENODEV);

	ret = isofs_probe(plat->desc, &plat->part);
	if (ret)
		return log_msg_ret("emp", ret);

	uc_priv->mounted = true;

	return 0;
}

static int isofs_vfs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("euu", -ENOTCONN);

	isofs_close();
	uc_priv->mounted = false;

	return 0;
}

static int isofs_vfs_lookup_dir(struct udevice *dev, const char *path,
				struct udevice **dirp)
{
	struct udevice *dir;
	int ret;

	ret = dir_add_probe(dev, DM_DRIVER_GET(isofs_vfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("eld", ret);

	*dirp = dir;

	return 0;
}

static const struct fs_ops isofs_vfs_ops = {
	.mount		= isofs_vfs_mount,
	.unmount	= isofs_vfs_unmount,
	.lookup_dir	= isofs_vfs_lookup_dir,
};

U_BOOT_DRIVER(isofs_fs) = {
	.name	= "isofs_fs",
	.id	= UCLASS_FS,
	.ops	= &isofs_vfs_ops,
};

/* isofs directory driver */

static int isofs_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dev);
	struct isofs_dir_priv *priv = dev_get_priv(dev);
	struct fs_dir_stream *iso_strm;
	const char *path;
	int ret;

	path = *uc_priv->path ? uc_priv->path : "/";
	ret = isofs_opendir(path, &iso_strm);
	if (ret)
		return log_msg_ret("edo", ret);

	priv->strm = iso_strm;

	return 0;
}

static int isofs_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
			  struct fs_dirent *dent)
{
	struct isofs_dir_priv *priv = dev_get_priv(dev);
	struct fs_dirent *iso_dent;
	int ret;

	ret = isofs_readdir(priv->strm, &iso_dent);
	if (ret)
		return ret;

	*dent = *iso_dent;

	return 0;
}

static int isofs_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct isofs_dir_priv *priv = dev_get_priv(dev);

	isofs_closedir(priv->strm);
	priv->strm = NULL;

	return 0;
}

/* isofs file driver */

/**
 * struct isofs_file_priv - Private info for isofs file devices
 *
 * @path: Full path within the isofs filesystem
 */
struct isofs_file_priv {
	char path[FILE_MAX_PATH_LEN];
};

static ssize_t isofs_vfs_read_iter(struct udevice *dev, struct iov_iter *iter,
				   loff_t pos)
{
	struct isofs_file_priv *priv = dev_get_priv(dev);
	loff_t actual;
	int ret;

	ret = isofs_read(priv->path, iter_iov_ptr(iter), pos,
			 iter_iov_avail(iter), &actual);
	if (ret)
		return log_msg_ret("efr", ret);
	iter_advance(iter, actual);

	return actual;
}

static struct file_ops isofs_file_ops = {
	.read_iter	= isofs_vfs_read_iter,
};

U_BOOT_DRIVER(isofs_vfs_file) = {
	.name		= "isofs_vfs_file",
	.id		= UCLASS_FILE,
	.ops		= &isofs_file_ops,
	.priv_auto	= sizeof(struct isofs_file_priv),
};

static int isofs_dir_open_file(struct udevice *dir, const char *leaf,
			       enum dir_open_flags_t oflags,
			       struct udevice **filp)
{
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(dir);
	char path[FILE_MAX_PATH_LEN];
	struct isofs_file_priv *priv;
	struct udevice *dev;
	loff_t size = 0;
	int ret;

	/* ISO 9660 is read-only */
	if (oflags != DIR_O_RDONLY)
		return log_msg_ret("eow", -EROFS);

	if (*uc_priv->path)
		snprintf(path, sizeof(path), "%s/%s", uc_priv->path, leaf);
	else
		snprintf(path, sizeof(path), "/%s", leaf);

	if (!isofs_exists(path))
		return log_msg_ret("eoe", -ENOENT);
	ret = isofs_size(path, &size);
	if (ret)
		return log_msg_ret("eos", ret);

	ret = file_add_probe(dir, DM_DRIVER_REF(isofs_vfs_file), leaf,
			     size, oflags, &dev);
	if (ret)
		return log_msg_ret("eop", ret);

	priv = dev_get_priv(dev);
	strlcpy(priv->path, path, sizeof(priv->path));
	*filp = dev;

	return 0;
}

static struct dir_ops isofs_dir_vfs_ops = {
	.open		= isofs_dir_open,
	.read		= isofs_dir_read,
	.close		= isofs_dir_close,
	.open_file	= isofs_dir_open_file,
};

U_BOOT_DRIVER(isofs_vfs_dir) = {
	.name		= "isofs_vfs_dir",
	.id		= UCLASS_DIR,
	.ops		= &isofs_dir_vfs_ops,
	.priv_auto	= sizeof(struct isofs_dir_priv),
};
