/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal VFS helpers - not part of the public API
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __VFS_INTERNAL_H
#define __VFS_INTERNAL_H

#include <file.h>

struct udevice;

/**
 * struct vfs_priv - VFS root FS device driver-private data
 *
 * @mount_count: Counter for unique mount device names
 * @cwd: Current working directory (absolute path)
 */
struct vfs_priv {
	int mount_count;
	char cwd[FILE_MAX_PATH_LEN];
};

/**
 * fs_mount_init() - Create a mount device
 *
 * Binds and probes a UCLASS_MOUNT device as a child of @vfs, linking
 * @dir to @fsdev.
 *
 * @vfs: VFS root FS device
 * @dir: UCLASS_DIR device that is the mount point
 * @fsdev: UCLASS_FS device to mount
 * Return: 0 if OK, -ve on error
 */
int fs_mount_init(struct udevice *vfs, struct udevice *dir,
		  struct udevice *fsdev);

/**
 * fs_mount_uninit() - Remove and unbind a mount device
 *
 * @mnt_dev: UCLASS_MOUNT device to destroy
 * Return: 0 if OK, -ve on error
 */
int fs_mount_uninit(struct udevice *mnt_dev);

#endif
