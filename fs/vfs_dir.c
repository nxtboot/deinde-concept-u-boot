// SPDX-License-Identifier: GPL-2.0
/*
 * VFS root directory driver
 *
 * Provides the directory driver for the VFS rootfs. The root directory
 * (path "") lists all mount-point directories as entries. Mount-point
 * directories themselves have no entries since access crosses into the
 * mounted filesystem.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <dir.h>
#include <dm.h>
#include <fs.h>
#include <vfs.h>
#include <fs_common.h>

static int vfs_rootfs_dir_open(struct udevice *dev,
			       struct fs_dir_stream *strm)
{
	strm->offset = 0;

	return 0;
}

/**
 * vfs_rootfs_dir_read() - Read the next directory entry
 *
 * For the root directory (path ""), iterates through sibling DIR devices
 * that have non-empty paths, returning each as a directory entry. For
 * mount-point directories, returns -ENOENT immediately since their
 * contents come from the mounted filesystem.
 */
static int vfs_rootfs_dir_read(struct udevice *dev,
			       struct fs_dir_stream *strm,
			       struct fs_dirent *dent)
{
	struct dir_uc_priv *dir_priv = dev_get_uclass_priv(dev);
	struct udevice *parent, *child;
	int idx = 0;

	/* Only the root dir has entries to list */
	if (*dir_priv->path)
		return -ENOENT;

	/* Walk children of the FS device, skipping to the current offset */
	parent = dev_get_parent(dev);
	device_foreach_child(child, parent) {
		struct dir_uc_priv *uc_priv;

		if (device_get_uclass_id(child) != UCLASS_DIR)
			continue;

		uc_priv = dev_get_uclass_priv(child);
		if (!uc_priv->path || !*uc_priv->path)
			continue;

		if (idx++ < strm->offset)
			continue;

		strlcpy(dent->name, uc_priv->path, sizeof(dent->name));
		dent->type = FS_DT_DIR;
		dent->size = 0;
		strm->offset++;

		return 0;
	}

	return -ENOENT;
}

static int vfs_rootfs_dir_close(struct udevice *dev,
				struct fs_dir_stream *strm)
{
	return 0;
}

static struct dir_ops vfs_rootfs_dir_ops = {
	.open	= vfs_rootfs_dir_open,
	.read	= vfs_rootfs_dir_read,
	.close	= vfs_rootfs_dir_close,
};

static int vfs_rootfs_dir_remove(struct udevice *dev)
{
	if (vfs_is_mount_point(dev))
		return log_msg_ret("drm", -EBUSY);

	return 0;
}

U_BOOT_DRIVER(vfs_rootfs_dir) = {
	.name	= "vfs_rootfs_dir",
	.id	= UCLASS_DIR,
	.ops	= &vfs_rootfs_dir_ops,
	.remove	= vfs_rootfs_dir_remove,
};
