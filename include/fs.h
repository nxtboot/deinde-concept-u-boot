/* SPDX-License-Identifier: GPL-2.0 */
/*
 * U-Boot Filesystem layer
 *
 * Models a filesystem which can be mounted and unmounted. It also allows a
 * directory to be looked up.
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#ifndef __FS_H
#define __FS_H

#include <fs_common.h>
#include <part.h>

struct udevice;

enum {
	/* Maximum length of the filesystem name */
	FS_MAX_NAME_LEN		= 128,
};

/**
 * struct fs_plat - Filesystem information
 *
 * For block-device-backed filesystems, @desc and @part identify the
 * underlying storage. Non-block filesystems (hostfs, rootfs) leave
 * @desc as NULL.
 *
 * @name: Name of the filesystem, or empty if not available
 * @desc: Block device descriptor, or NULL if not block-backed
 * @part: Partition information (valid only when @desc is non-NULL)
 */
struct fs_plat {
	char name[FS_MAX_NAME_LEN];
	struct blk_desc *desc;
	struct disk_partition part;
};

/**
 * struct fs_priv - Private information for the FS devices
 *
 * @mounted: true if mounted
 */
struct fs_priv {
	bool mounted;
};

struct fs_ops {
	/**
	 * mount() - Mount the filesystem
	 *
	 * @dev: Filesystem device
	 * Return 0 if OK, -EISCONN if already mounted, other -ve on error
	 */
	int (*mount)(struct udevice *dev);

	/**
	 * unmount() - Unmount the filesystem
	 *
	 * @dev: Filesystem device
	 * Return 0 if OK, -ENOTCONN if not mounted, other -ve on error
	 */
	int (*unmount)(struct udevice *dev);

	/**
	 * lookup_dir() - Look up a directory on a filesystem
	 *
	 * This should not set up the uclass-private data; this is done by
	 * fs_lookup_dir()
	 *
	 * @dev: Filesystem device
	 * @path: Path to look up, "" for the root
	 * @dirp: Returns associated directory device, creating if necessary
	 * Return 0 if OK, -ENOENT, other -ve on error
	 */
	int (*lookup_dir)(struct udevice *dev, const char *path,
			  struct udevice **dirp);
};

/* Get access to a filesystem's operations */
#define fs_get_ops(dev)		((struct fs_ops *)(dev)->driver->ops)

/**
 * fs_mount() - Mount the filesystem
 *
 * @dev: Filesystem device
 * Return 0 if OK, -EISCONN if already mounted, other -ve on error
 */
int fs_mount(struct udevice *dev);

/**
 * fs_unmount() - Unmount the filesystem
 *
 * @dev: Filesystem device
 * Return 0 if OK, -ENOTCONN if not mounted, other -ve on error
 */
int fs_unmount(struct udevice *dev);

/**
 * fs_lookup_dir() - Look up a directory on a filesystem
 *
 * If a new directory-device is created, its uclass data is set up also
 *
 * @dev: Filesystem device
 * @path: Path to look up, "" or "/" for the root
 * @dirp: Returns associated directory device, creating if necessary
 * Return 0 if OK, -ENOENT, other -ve on error
 */
int fs_lookup_dir(struct udevice *dev, const char *path, struct udevice **dirp);

/**
 * fs_split_path() - Get a list of subdirs in a filename
 *
 * For example, '/path/to/fred' returns an alist containing allocated strings
 * 'path' and 'to', with \*leafp pointing to the 'f'
 *
 * @fname: Filename to parse
 * @subdirp: Returns an allocating string containing the subdirs, or "/" if none
 * @leafp: Returns a pointer to the leaf filename, within @fname
 */
int fs_split_path(const char *fname, char **subdirp, const char **leafp);

#endif
