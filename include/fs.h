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

struct fs_statfs;
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
 * @part_num: Partition number (valid only when @desc is non-NULL)
 * @part: Partition information (valid only when @desc is non-NULL)
 */
struct fs_plat {
	char name[FS_MAX_NAME_LEN];
	struct blk_desc *desc;
	int part_num;
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

	/**
	 * rename() - Rename or move a file or directory
	 *
	 * @dev: Filesystem device
	 * @old_path: Current path
	 * @new_path: New path
	 * Return 0 if OK, -ve on error
	 */
	int (*rename)(struct udevice *dev, const char *old_path,
		      const char *new_path);

	/**
	 * ln() - Create a symbolic link
	 *
	 * @dev: Filesystem device
	 * @path: Path of symlink to create
	 * @target: Target the symlink points to
	 * Return 0 if OK, -ve on error
	 */
	int (*ln)(struct udevice *dev, const char *path, const char *target);

	/**
	 * readlink() - Read the target of a symbolic link
	 *
	 * @dev: Filesystem device
	 * @path: Path to the symbolic link
	 * @buf: Buffer to receive the target path
	 * @size: Size of buffer
	 * Return: length of target string, or -ve on error
	 */
	int (*readlink)(struct udevice *dev, const char *path, char *buf,
			int size);

	/**
	 * statfs() - Get filesystem statistics
	 *
	 * @dev: Filesystem device
	 * @stats: Returns filesystem statistics
	 * Return 0 if OK, -ve on error
	 */
	int (*statfs)(struct udevice *dev, struct fs_statfs *stats);

	/**
	 * unlink() - Delete a file
	 *
	 * @dev: Filesystem device
	 * @path: Path of the file to delete
	 * Return 0 if OK, -ve on error
	 */
	int (*unlink)(struct udevice *dev, const char *path);

	/**
	 * mkdir() - Create a directory
	 *
	 * @dev: Filesystem device
	 * @path: Path of the directory to create
	 * Return 0 if OK, -ve on error
	 */
	int (*mkdir)(struct udevice *dev, const char *path);
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
 * fs_do_ln() - Create a symbolic link on a filesystem
 *
 * @dev: Filesystem device
 * @path: Path of symlink to create (within the filesystem)
 * @target: Target the symlink points to
 * Return: 0 if OK, -ENOSYS if not supported, other -ve on error
 */
int fs_do_ln(struct udevice *dev, const char *path, const char *target);

/**
 * fs_do_rename() - Rename or move a file on a filesystem
 *
 * Both paths must be within the same filesystem.
 *
 * @dev: Filesystem device
 * @old_path: Current path (within the filesystem)
 * @new_path: New path (within the filesystem)
 * Return: 0 if OK, -ENOSYS if not supported, other -ve on error
 */
int fs_do_rename(struct udevice *dev, const char *old_path,
		 const char *new_path);

/**
 * fs_readlink() - Read a symbolic link target on a filesystem
 *
 * @dev: Filesystem device
 * @path: Path to the symbolic link (within the filesystem)
 * @buf: Buffer to receive the target path
 * @size: Size of buffer
 * Return: length of target, -ENOSYS if not supported, other -ve on error
 */
int fs_readlink(struct udevice *dev, const char *path, char *buf, int size);

/**
 * fs_do_statfs() - Get filesystem statistics
 *
 * @dev: Filesystem device
 * @stats: Returns filesystem statistics
 * Return: 0 if OK, -ENOSYS if not supported, other -ve on error
 */
int fs_do_statfs(struct udevice *dev, struct fs_statfs *stats);

/**
 * fs_do_unlink() - Delete a file on a filesystem
 *
 * @dev: Filesystem device
 * @path: Path of the file to delete (within the filesystem)
 * Return: 0 if OK, -ENOSYS if not supported, other -ve on error
 */
int fs_do_unlink(struct udevice *dev, const char *path);

/**
 * fs_do_mkdir() - Create a directory on a filesystem
 *
 * @dev: Filesystem device
 * @path: Path of the directory to create (within the filesystem)
 * Return: 0 if OK, -ENOSYS if not supported, other -ve on error
 */
int fs_do_mkdir(struct udevice *dev, const char *path);

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

/**
 * fs_split_path_inplace() - Split a path into directory and leaf in place
 *
 * Modifies @fname by null-terminating at the last '/'. Sets @dirp to
 * point to the directory part and @leafp to the leaf. If there is no
 * '/', @dirp is set to "" and @leafp points to @fname unchanged.
 *
 * @fname: Path to split (modified in place when it contains '/')
 * @dirp: Returns pointer to the directory part
 * @leafp: Returns pointer to the leaf filename
 */
void fs_split_path_inplace(char *fname, const char **dirp,
			   const char **leafp);

#endif
