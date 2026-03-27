// SPDX-License-Identifier: GPL-2.0
/*
 * Mount device driver for the VFS layer
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <dm.h>
#include <malloc.h>
#include <vfs.h>
#include "vfs_internal.h"
#include <dm/device-internal.h>
#include <dm/lists.h>

int fs_mount_init(struct udevice *vfs, struct udevice *dir,
		  struct udevice *fsdev)
{
	struct vfs_priv *priv = dev_get_priv(vfs);
	struct vfsmount *mnt;
	char dev_name[30];
	struct udevice *dev;
	char *str;
	int ret;

	snprintf(dev_name, sizeof(dev_name), "mount.%d", priv->mount_count);
	str = strdup(dev_name);
	if (!str)
		return log_msg_ret("fms", -ENOMEM);

	ret = device_bind_driver(vfs, "mount", str, &dev);
	if (ret) {
		free(str);
		return log_msg_ret("fmb", ret);
	}
	device_set_name_alloced(dev);

	ret = device_probe(dev);
	if (ret) {
		device_unbind(dev);
		return log_msg_ret("fmp", ret);
	}

	mnt = dev_get_uclass_priv(dev);
	mnt->dir = dir;
	mnt->target = fsdev;
	priv->mount_count++;

	return 0;
}

int fs_mount_uninit(struct udevice *mnt_dev)
{
	int ret;

	ret = device_remove(mnt_dev, DM_REMOVE_NORMAL);
	if (ret)
		return log_msg_ret("fdr", ret);

	ret = device_unbind(mnt_dev);
	if (ret)
		return log_msg_ret("fdb", ret);

	return 0;
}

U_BOOT_DRIVER(mount) = {
	.name	= "mount",
	.id	= UCLASS_MOUNT,
};
