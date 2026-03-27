/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Virtual Filesystem layer for U-Boot
 *
 * Provides a unified path namespace with mount points, inspired by the
 * Linux VFS but heavily cut down for U-Boot's needs.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __VFS_H
#define __VFS_H

struct udevice;

/**
 * struct vfsmount - A mount point in the VFS
 *
 * This is the UCLASS_MOUNT per-device uclass-private data. All mount
 * devices are children of the VFS root FS device. Each links a mount-point
 * directory to a UCLASS_FS device.
 *
 * @dir: UCLASS_DIR device that is the mount point
 * @target: UCLASS_FS device that is mounted here
 */
struct vfsmount {
	struct udevice *dir;
	struct udevice *target;
};

/**
 * vfs_init() - Initialise the VFS
 *
 * Creates the VFS root directory device. Normally called automatically
 * via EVT_LAST_STAGE_INIT during boot. May also be called directly in
 * tests after a DM tree reset.
 *
 * Return: 0 if OK, -ve on error
 */
int vfs_init(void);

/**
 * vfs_root() - Get the VFS root FS device
 *
 * Return: VFS root FS device, or NULL if not initialised
 */
struct udevice *vfs_root(void);

#endif
