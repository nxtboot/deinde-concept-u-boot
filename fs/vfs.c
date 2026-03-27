// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Filesystem layer
 *
 * Manages a mount tree using UCLASS_MOUNT devices as children of
 * UCLASS_DIR devices, providing unified path resolution across mounted
 * filesystems. Inspired by the Linux VFS.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_MOUNT

#include <dir.h>
#include <dm.h>
#include <event.h>
#include <fs.h>
#include <vfs.h>
#include "vfs_internal.h"
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <dm/uclass-internal.h>

/* VFS root filesystem - provides an empty root directory */

static int vfs_rootfs_mount(struct udevice *dev)
{
	struct fs_priv *uc_priv = dev_get_uclass_priv(dev);

	if (uc_priv->mounted)
		return log_msg_ret("rfm", -EISCONN);

	uc_priv->mounted = true;

	return 0;
}

static int vfs_rootfs_unmount(struct udevice *dev)
{
	return log_msg_ret("rfu", -EBUSY);
}

static int vfs_rootfs_lookup_dir(struct udevice *dev, const char *path,
				 struct udevice **dirp)
{
	struct dir_uc_priv *uc_priv;
	struct udevice *child, *dir;
	int ret;

	/* Check for an existing dir with this path */
	device_foreach_child(child, dev) {
		if (device_get_uclass_id(child) == UCLASS_DIR) {
			uc_priv = dev_get_uclass_priv(child);
			if (uc_priv->path && !strcmp(uc_priv->path, path)) {
				*dirp = child;
				return 0;
			}
		}
	}

	/* Create a new dir */
	ret = dir_add_probe(dev, DM_DRIVER_GET(vfs_rootfs_dir), path, &dir);
	if (ret)
		return log_msg_ret("rfD", ret);

	*dirp = dir;

	return 0;
}

/* Exported functions */

struct udevice *vfs_root(void)
{
	struct udevice *dev;

	if (uclass_find_device_by_name(UCLASS_FS, "vfs_rootfs", &dev))
		return NULL;
	if (!device_active(dev))
		return NULL;

	return dev;
}

int vfs_init(void)
{
	struct udevice *dev;
	int ret;

	/* Already initialised? */
	dev = vfs_root();
	if (dev)
		return 0;

	ret = device_bind_driver(dm_root(), "vfs_rootfs", "vfs_rootfs", &dev);
	if (ret)
		return log_msg_ret("vib", ret);

	ret = device_probe(dev);
	if (ret)
		return log_msg_ret("vip", ret);

	ret = fs_mount(dev);
	if (ret)
		return log_msg_ret("vim", ret);

	return 0;
}

static const struct fs_ops vfs_rootfs_ops = {
	.mount		= vfs_rootfs_mount,
	.unmount	= vfs_rootfs_unmount,
	.lookup_dir	= vfs_rootfs_lookup_dir,
};

U_BOOT_DRIVER(vfs_rootfs) = {
	.name		= "vfs_rootfs",
	.id		= UCLASS_FS,
	.ops		= &vfs_rootfs_ops,
	.priv_auto	= sizeof(struct vfs_priv),
};

EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, vfs_init);
