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
#include <file.h>
#include <fs.h>
#include <fs_common.h>
#include <malloc.h>
#include <vfs.h>
#include "vfs_internal.h"
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <dm/uclass-internal.h>

#define vfs_foreach_mount(mnt, pos) \
	for (uclass_first_device(UCLASS_MOUNT, &(pos)); \
	     (pos) && ((mnt) = dev_get_uclass_priv(pos)); \
	     uclass_next_device(&(pos)))

/**
 * find_mount() - Check whether a directory is a mount point
 *
 * @dir: UCLASS_DIR device to check
 * @mntp: Returns the UCLASS_MOUNT device if found
 * Return: 0 if found, -ENOENT if not
 */
static int find_mount(struct udevice *dir, struct udevice **mntp)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		if (mnt->dir == dir) {
			*mntp = dev;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * find_mount_by_target() - Find the mount for a given FS device
 *
 * @fsdev: UCLASS_FS device to search for
 * @mntp: Returns the UCLASS_MOUNT device if found
 * Return: 0 if found, -ENOENT if not
 */
static int find_mount_by_target(struct udevice *fsdev, struct udevice **mntp)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		if (mnt->target == fsdev) {
			*mntp = dev;
			return 0;
		}
	}

	return -ENOENT;
}

/**
 * walk_path() - Walk a path following mount points
 *
 * For each path component, looks up the directory in the current
 * filesystem and checks if it is a mount point. Stops when a component
 * cannot be looked up or is not a mount point.
 *
 * @path: Path to walk (without leading '/')
 * @start: Starting FS device
 * @mntp: Returns the deepest mount found, or NULL if none
 * @remainp: Returns pointer to the remaining unresolved path
 * Return: The FS after the deepest mount crossed (same as @start if none)
 */
static struct udevice *walk_path(const char *path, struct udevice *start,
				 struct udevice **mntp, const char **remainp)
{
	struct udevice *best = NULL, *cur_fs = start;
	const char *best_remain = path;
	const char *p = path;

	while (*p) {
		char component[FS_DIRENT_NAME_LEN];
		struct udevice *comp_dir, *mnt_dev;
		struct vfsmount *mnt;
		const char *slash;
		int len;

		slash = strchr(p, '/');
		len = slash ? slash - p : strlen(p);
		if (len >= sizeof(component))
			break;

		memcpy(component, p, len);
		component[len] = '\0';

		if (fs_lookup_dir(cur_fs, component, &comp_dir))
			break;

		if (find_mount(comp_dir, &mnt_dev))
			break;

		/* Found a mount - record it and cross into the FS */
		best = mnt_dev;
		p += len;
		if (*p == '/')
			p++;
		best_remain = p;

		mnt = dev_get_uclass_priv(mnt_dev);
		cur_fs = mnt->target;
	}

	*mntp = best;
	*remainp = best_remain;

	return cur_fs;
}

/**
 * vfs_mount_path() - Build the full path for a mount device
 *
 * Walks up the device tree to reconstruct the absolute path, using
 * dir_uc_priv->path from each mount's directory to get the component name.
 *
 * @mnt_dev: UCLASS_MOUNT device
 * @buf: Buffer to write path into
 * @size: Size of buffer
 * Return: 0 if OK, -ve on error
 */
static int vfs_mount_path(struct udevice *mnt_dev, char *buf, int size)
{
	struct vfsmount *mnt = dev_get_uclass_priv(mnt_dev);
	struct dir_uc_priv *uc_priv = dev_get_uclass_priv(mnt->dir);
	struct udevice *parent_fs = dev_get_parent(mnt->dir);

	if (parent_fs == vfs_root()) {
		snprintf(buf, size, "/%s", uc_priv->path);
	} else {
		char parent_path[FILE_MAX_PATH_LEN];
		struct udevice *pdev;
		int ret;

		ret = find_mount_by_target(parent_fs, &pdev);
		if (ret)
			return ret;

		ret = vfs_mount_path(pdev, parent_path, sizeof(parent_path));
		if (ret)
			return ret;
		snprintf(buf, size, "%s/%s", parent_path, uc_priv->path);
	}

	return 0;
}

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

int vfs_find_mount(struct udevice *vfs, const char *path, struct udevice **mntp,
		   const char **subpathp)
{
	struct udevice *best;
	const char *p;

	p = path;
	if (*p == '/')
		p++;

	walk_path(p, vfs, &best, subpathp);

	if (!best) {
		if (!*p) {
			*mntp = NULL;
			return 0;
		}
		return log_msg_ret("vfn", -ENOENT);
	}

	*mntp = best;

	return 0;
}

int vfs_resolve(struct udevice *vfs, const char *path,
		struct udevice **dirp)
{
	struct udevice *cur_fs, *best;
	const char *remain;

	if (!path || *path != '/')
		return log_msg_ret("vrp", -EINVAL);

	cur_fs = walk_path(path + 1, vfs, &best, &remain);

	/* Remaining path must be at most one component (the target dir) */
	if (strchr(remain, '/'))
		return log_msg_ret("vrm", -ENOENT);

	return fs_lookup_dir(cur_fs, remain, dirp);
}

int vfs_mount(struct udevice *vfs, struct udevice *dir, struct udevice *fsdev)
{
	int ret;

	ret = fs_mount(fsdev);
	if (ret && ret != -EISCONN)
		return log_msg_ret("vmm", ret);

	ret = fs_mount_init(vfs, dir, fsdev);
	if (ret) {
		fs_unmount(fsdev);
		return log_msg_ret("vmc", ret);
	}

	return 0;
}

int vfs_umount(struct udevice *mnt_dev)
{
	struct vfsmount *mnt = dev_get_uclass_priv(mnt_dev);
	int ret;

	ret = fs_unmount(mnt->target);
	if (ret && ret != -ENOTCONN)
		return log_msg_ret("vuu", ret);

	ret = fs_mount_uninit(mnt_dev);
	if (ret)
		return log_msg_ret("vud", ret);

	return 0;
}

int vfs_umount_path(struct udevice *vfs, const char *path)
{
	struct udevice *mnt_dev;
	const char *subpath;
	int ret;

	ret = vfs_find_mount(vfs, path, &mnt_dev, &subpath);
	if (ret)
		return log_msg_ret("vuf", ret);

	/* Make sure the entire path was consumed (exact match) */
	if (!mnt_dev || *subpath)
		return log_msg_ret("vup", -ENOENT);

	return vfs_umount(mnt_dev);
}

bool vfs_is_mount_point(struct udevice *dir)
{
	struct udevice *mnt;

	return !find_mount(dir, &mnt);
}

void vfs_print_mounts(void)
{
	struct vfsmount *mnt;
	struct udevice *dev;

	vfs_foreach_mount(mnt, dev) {
		char path[FILE_MAX_PATH_LEN];

		if (!vfs_mount_path(dev, path, sizeof(path)))
			printf("%-20s %s\n", path, mnt->target->name);
	}
}

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
