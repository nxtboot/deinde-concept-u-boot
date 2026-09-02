// SPDX-License-Identifier: GPL-2.0+
/*
 * CBFS filesystem driver for the VFS layer
 *
 * Provides UCLASS_FS, UCLASS_DIR and UCLASS_FILE devices over the CBFS which
 * cbfsinit has read, following the same pattern as isofs/fs.c
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_FS

#include <cbfs.h>
#include <dir.h>
#include <dm.h>
#include <file.h>
#include <fs.h>
#include <iovec.h>
#include <vfs.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <dm/uclass-internal.h>
#include <linux/errno.h>

/**
 * struct cbfs_dir_priv - Private info for the CBFS directory device
 *
 * @file: Next file to report from read(), or NULL when they are all done
 */
struct cbfs_dir_priv {
	const struct cbfs_cachenode *file;
};

/**
 * struct cbfs_file_priv - Private info for a CBFS file device
 *
 * @file: The file this device refers to
 */
struct cbfs_file_priv {
	const struct cbfs_cachenode *file;
};

static int cbfs_vfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (uc_priv->mounted)
		return log_msg_ret("cmm", -EISCONN);

	/*
	 * CBFS is in the ROM rather than on a block device, so there is
	 * nothing to probe. It does have to have been read, since only
	 * cbfsinit knows where in the ROM it is.
	 */
	if (!file_cbfs_get_header())
		return log_msg_ret("cmh", -ENOENT);

	uc_priv->mounted = true;

	return 0;
}

static int cbfs_vfs_unmount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (!uc_priv->mounted)
		return log_msg_ret("cuu", -ENOTCONN);
	uc_priv->mounted = false;

	return 0;
}

static int cbfs_vfs_lookup_dir(struct udevice *dev, const char *path,
			       struct udevice **dirp)
{
	struct udevice *dir;
	int ret;

	/* CBFS holds a flat list of files, so the root is the only directory */
	if (*path && strcmp(path, "/"))
		return log_msg_ret("cld", -ENOENT);

	ret = dir_add_probe(dev, DM_DRIVER_GET(cbfs_vfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("clD", ret);

	*dirp = dir;

	return 0;
}

static const struct fs_ops cbfs_vfs_ops = {
	.mount		= cbfs_vfs_mount,
	.unmount	= cbfs_vfs_unmount,
	.lookup_dir	= cbfs_vfs_lookup_dir,
};

U_BOOT_DRIVER(cbfs_fs) = {
	.name	= "cbfs_fs",
	.id	= UCLASS_FS,
	.ops	= &cbfs_vfs_ops,
};

/* CBFS directory driver */

static int cbfs_dir_open(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct cbfs_dir_priv *priv = dev_get_priv(dev);

	priv->file = file_cbfs_get_first();

	return 0;
}

static int cbfs_dir_read(struct udevice *dev, struct fs_dir_stream *strm,
			 struct fs_dirent *dent)
{
	struct cbfs_dir_priv *priv = dev_get_priv(dev);

	if (!priv->file)
		return -ENOENT;

	memset(dent, '\0', sizeof(*dent));
	dent->type = FS_DT_REG;
	dent->size = file_cbfs_size(priv->file);
	strlcpy(dent->name, file_cbfs_name(priv->file), sizeof(dent->name));

	file_cbfs_get_next(&priv->file);

	return 0;
}

static int cbfs_dir_close(struct udevice *dev, struct fs_dir_stream *strm)
{
	struct cbfs_dir_priv *priv = dev_get_priv(dev);

	priv->file = NULL;

	return 0;
}

/* CBFS file driver */

static ssize_t cbfs_vfs_read_iter(struct udevice *dev, struct iov_iter *iter,
				  loff_t pos)
{
	struct cbfs_file_priv *priv = dev_get_priv(dev);
	ulong size;

	size = file_cbfs_size(priv->file);
	if (pos > size)
		return log_msg_ret("cfp", -EINVAL);
	size -= pos;
	if (size > iter_iov_avail(iter))
		size = iter_iov_avail(iter);

	memcpy(iter_iov_ptr(iter), priv->file->data + pos, size);
	iter_advance(iter, size);

	return size;
}

static struct file_ops cbfs_file_ops = {
	.read_iter	= cbfs_vfs_read_iter,
};

U_BOOT_DRIVER(cbfs_vfs_file) = {
	.name		= "cbfs_vfs_file",
	.id		= UCLASS_FILE,
	.ops		= &cbfs_file_ops,
	.priv_auto	= sizeof(struct cbfs_file_priv),
};

static int cbfs_dir_open_file(struct udevice *dir, const char *leaf,
			      enum dir_open_flags_t oflags,
			      struct udevice **filp)
{
	const struct cbfs_cachenode *file;
	struct cbfs_file_priv *priv;
	struct udevice *dev;
	int ret;

	/* CBFS is in read-only memory */
	if (oflags != DIR_O_RDONLY)
		return log_msg_ret("cow", -EROFS);

	file = file_cbfs_find(leaf);
	if (!file)
		return log_msg_ret("coe", -ENOENT);

	ret = file_add_probe(dir, DM_DRIVER_REF(cbfs_vfs_file), leaf,
			     file_cbfs_size(file), oflags, &dev);
	if (ret)
		return log_msg_ret("cop", ret);

	priv = dev_get_priv(dev);
	priv->file = file;
	*filp = dev;

	return 0;
}

static struct dir_ops cbfs_dir_vfs_ops = {
	.open		= cbfs_dir_open,
	.read		= cbfs_dir_read,
	.close		= cbfs_dir_close,
	.open_file	= cbfs_dir_open_file,
};

U_BOOT_DRIVER(cbfs_vfs_dir) = {
	.name		= "cbfs_vfs_dir",
	.id		= UCLASS_DIR,
	.ops		= &cbfs_dir_vfs_ops,
	.priv_auto	= sizeof(struct cbfs_dir_priv),
};

int cbfs_vfs_bind(void)
{
	struct udevice *dev;
	int ret;

	/* cbfsinit may be run more than once, with a different ROM each time */
	if (!uclass_find_device_by_name(UCLASS_FS, "cbfs", &dev))
		return 0;

	ret = device_bind_driver(dm_root(), "cbfs_fs", "cbfs", &dev);
	if (ret)
		return log_msg_ret("cvb", ret);

	return 0;
}
