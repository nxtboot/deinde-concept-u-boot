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

/**
 * vfs_resolve() - Resolve a path to a directory
 *
 * Walks the path, following mount points along the way. For each
 * component, looks up the directory in the current filesystem. If the
 * directory does not exist (e.g. in the VFS rootfs), it is created.
 *
 * For "/host", looks up (or creates) "host" in the VFS rootfs.
 * For "/mnt/data", follows the mount at /mnt, then looks up "data"
 * in the mounted filesystem.
 *
 * @vfs: VFS root FS device
 * @path: Absolute path (must start with '/')
 * @dirp: Returns the UCLASS_DIR device for the final component
 * Return: 0 if OK, -ve on error
 */
int vfs_resolve(struct udevice *vfs, const char *path,
		struct udevice **dirp);

/**
 * vfs_mount() - Mount a filesystem at a directory
 *
 * Creates a UCLASS_MOUNT device linking @dir to @fsdev.
 *
 * @vfs: VFS root FS device
 * @dir: UCLASS_DIR device for the mount point
 * @fsdev: UCLASS_FS device to mount
 * Return: 0 if OK, -ve on error
 */
int vfs_mount(struct udevice *vfs, struct udevice *dir, struct udevice *fsdev);

/**
 * vfs_umount() - Unmount a filesystem
 *
 * @mnt_dev: UCLASS_MOUNT device to unmount
 * Return: 0 if OK, -ve on error
 */
int vfs_umount(struct udevice *mnt_dev);

/**
 * vfs_umount_path() - Unmount the filesystem at a path
 *
 * @vfs: VFS root FS device
 * @path: Mount point to remove
 * Return: 0 if OK, -ENOENT if not mounted, other -ve on error
 */
int vfs_umount_path(struct udevice *vfs, const char *path);

/**
 * vfs_find_mount() - Find the mount covering a path
 *
 * Walks the mount tree from the VFS root, following mount points for
 * each path component. Returns the deepest mount and the remaining
 * subpath.
 *
 * @vfs: VFS root FS device
 * @path: Absolute path to resolve
 * @mntp: Returns the UCLASS_MOUNT device
 * @subpathp: Returns pointer into @path for the remaining path within the
 *	mounted filesystem
 * Return: 0 if OK (with @mntp set to NULL if path is the VFS root),
 *	-ENOENT if no mount covers this path
 */
int vfs_find_mount(struct udevice *vfs, const char *path,
		   struct udevice **mntp, const char **subpathp);

/**
 * vfs_print_mounts() - Print all current mounts
 */
void vfs_print_mounts(void);

#endif
