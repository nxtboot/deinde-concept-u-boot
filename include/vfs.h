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

#include <dir.h>

struct blk_desc;
struct cmd_tbl;
struct disk_partition;
struct fs_dirent;
struct fs_statfs;
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
 * vfs_chdir() - Change the current working directory
 *
 * @path: New directory (absolute or relative to current cwd)
 * Return: 0 if OK, -ve on error
 */
int vfs_chdir(const char *path);

/**
 * vfs_getcwd() - Get the current working directory
 *
 * Return: pointer to the cwd string (static buffer, do not free)
 */
const char *vfs_getcwd(void);

/**
 * vfs_path_resolve() - Resolve a path against a given working directory
 *
 * If @path starts with '/', it is used as an absolute path. Otherwise it
 * is joined with @cwd. In both cases, '.' and '..' components are resolved.
 *
 * @cwd: Current working directory (must be absolute)
 * @path: Input path (absolute or relative), or NULL/empty for cwd
 * @buf: Buffer to write the resolved absolute path
 * @size: Size of @buf
 * Return: pointer to @buf
 */
const char *vfs_path_resolve(const char *cwd, const char *path,
			     char *buf, int size);

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
 * vfs_is_mount_point() - Check whether a directory is a mount point
 *
 * @dir: UCLASS_DIR device to check
 * Return: true if this directory has an active mount, false otherwise
 */
bool vfs_is_mount_point(struct udevice *dir);

/**
 * vfs_print_mounts() - Print all current mounts
 */
void vfs_print_mounts(void);

/**
 * vfs_ls() - List directory contents
 *
 * Resolves the path through the mount table and lists directory entries
 * using dir_open(), dir_read() and dir_close().
 *
 * @path: Absolute path to list, or "/" for root
 * Return: 0 if OK, -ve on error
 */
int vfs_ls(const char *path);

/**
 * vfs_open_file() - Open a file by absolute VFS path
 *
 * Resolves the path through the mount tree, splits into directory and leaf,
 * then opens the file.
 *
 * @path: Absolute VFS path to the file
 * @oflags: Open flags (DIR_O_RDONLY, DIR_O_WRONLY, DIR_O_RDWR)
 * @filp: Returns the UCLASS_FILE device
 * Return: 0 if OK, -ve on error
 */
int vfs_open_file(const char *path, enum dir_open_flags_t oflags,
		  struct udevice **filp);

/**
 * vfs_stat() - Get information about a file or directory
 *
 * Looks up the named entry in its parent directory and fills in a
 * struct fs_dirent with type, size and timestamps. Handles both files
 * and directories. Calls vfs_init() if needed.
 *
 * @path: Absolute or relative VFS path
 * @dent: Returns the directory entry information
 * Return: 0 if OK, -ve on error
 */
int vfs_stat(const char *path, struct fs_dirent *dent);

/**
 * vfs_mkdir() - Create a directory by absolute or relative VFS path
 *
 * @path: Path of the directory to create
 * Return: 0 if OK, -ve on error
 */
int vfs_mkdir(const char *path);

/**
 * vfs_unlink() - Delete a file by absolute or relative VFS path
 *
 * @path: Path of the file to delete
 * Return: 0 if OK, -ve on error
 */
int vfs_unlink(const char *path);

/**
 * vfs_rename() - Rename or move a file or directory
 *
 * Both paths must be on the same mounted filesystem.
 *
 * @old_path: Current absolute or relative VFS path
 * @new_path: New absolute or relative VFS path
 * Return: 0 if OK, -EXDEV if paths are on different mounts, other -ve on error
 */
/**
 * vfs_ln() - Create a symbolic link
 *
 * @path: Absolute or relative VFS path for the new symlink
 * @target: Target path the symlink points to
 * Return: 0 if OK, -ve on error
 */
int vfs_ln(const char *path, const char *target);

int vfs_rename(const char *old_path, const char *new_path);

/**
 * vfs_readlink() - Read a symbolic link target
 *
 * @path: Absolute or relative VFS path to the symlink
 * @buf: Buffer to receive the target path
 * @size: Size of buffer
 * Return: length of target, or -ve on error
 */
int vfs_readlink(const char *path, char *buf, int size);

/**
 * vfs_statfs() - Get filesystem statistics for a mount point
 *
 * @path: Absolute or relative VFS path (any path within the mount)
 * @stats: Returns filesystem statistics
 * Return: 0 if OK, -ve on error
 */
int vfs_statfs(const char *path, struct fs_statfs *stats);

/**
 * fs_mount_blkdev_auto() - Auto-detect and mount a filesystem from a block
 *	device
 *
 * Tries each UCLASS_FS driver with a "_fs" suffix in turn until one
 * succeeds.
 *
 * @desc: Block device descriptor
 * @part: Partition information
 * @mountpoint: Absolute path to mount at
 * Return: 0 if OK, -ENODEV if no FS type matched, other -ve on error
 */
int fs_mount_blkdev_auto(struct blk_desc *desc, int part_num,
			 struct disk_partition *part, const char *mountpoint);

/**
 * fs_mount_blkdev() - Create and mount a FS device from a block device
 *
 * Uses a naming convention to find the right driver: filesystem type "ext4"
 * maps to driver "ext4_fs". The block device and partition info are stored
 * in the generic struct fs_plat so that any block-backed FS driver can
 * read them.
 *
 * @type: Filesystem type name (e.g. "ext4")
 * @desc: Block device descriptor
 * @part: Partition information
 * @mountpoint: Absolute path to mount at
 * Return: 0 if OK, -ve on error
 */
int fs_mount_blkdev(const char *type, struct blk_desc *desc, int part_num,
		    struct disk_partition *part, const char *mountpoint);

/**
 * vfs_print_mounts() - Print all current mounts
 */
void vfs_print_mounts(void);

#ifdef CONFIG_AUTO_COMPLETE
/**
 * vfs_complete() - Complete a partial VFS path
 *
 * Suitable for use as (or called from) a U_BOOT_CMD complete callback.
 * Fills @cmdv with matching directory entries.
 *
 * @buf: Scratch buffer (must be at least FILE_MAX_PATH_LEN bytes)
 * @path: Partial path to complete (absolute or relative)
 * @maxv: Maximum number of entries in @cmdv
 * @cmdv: Output array of matching names (NULL-terminated)
 * Return: number of matches
 */
int vfs_complete(char *buf, const char *path, int maxv, char *cmdv[]);

/**
 * vfs_cmd_complete() - Complete callback for commands taking a VFS path
 *
 * Completes the last argument as a VFS path. Can be passed directly to
 * U_BOOT_CMD_COMPLETE.
 */
int vfs_cmd_complete(int argc, char *const argv[], char last_char,
		     int maxv, char *cmdv[]);
#endif

#endif
