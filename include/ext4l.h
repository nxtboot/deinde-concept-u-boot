/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * ext4l filesystem interface
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#ifndef __EXT4L_H__
#define __EXT4L_H__

#include <part.h>

struct blk_desc;
struct fs_dir_stream;
struct fs_dirent;
struct fs_statfs;
struct super_block;
struct udevice;

/**
 * struct ext4l_state - per-mount state for the ext4l driver
 *
 * @blk: Block device (udevice) for buffer I/O
 * @partition: Partition info
 * @sb: Superblock pointer
 * @open_dirs: Count of open directory streams (prevents unmount)
 * @mounted: Whether a filesystem is currently mounted
 */
struct ext4l_state {
	struct udevice *blk;
	struct disk_partition partition;
	struct super_block *sb;
	int open_dirs;
	bool mounted;
};

/* Select op when EXT4_WRITE is enabled, fallback otherwise */
#if CONFIG_IS_ENABLED(EXT4_WRITE)
#define ext4l_op_ptr(op, fallback)	op
#else
#define ext4l_op_ptr(op, fallback)	fallback
#endif

/**
 * ext4l_mount() - Mount an ext4 filesystem
 *
 * @state: Per-mount state to initialise
 * @dev: Block device (struct udevice)
 * @fs_partition: Partition information
 * Return: 0 on success, -EINVAL if no device or invalid magic,
 *	   -ENOMEM on allocation failure, -EIO on read error
 */
int ext4l_mount(struct ext4l_state *state, struct udevice *dev,
		struct disk_partition *fs_partition);

/**
 * ext4l_probe() - Legacy probe: mount using the global state
 *
 * @fs_dev_desc: Block device descriptor
 * @fs_partition: Partition information
 * Return: 0 on success, negative on error
 */
int ext4l_probe(struct blk_desc *fs_dev_desc,
		struct disk_partition *fs_partition);

/**
 * ext4l_umount() - Unmount an ext4 filesystem
 *
 * @state: Per-mount state to tear down
 */
void ext4l_umount(struct ext4l_state *state);

/**
 * ext4l_close() - Legacy close: unmount using the global state
 */
void ext4l_close(void);

/**
 * ext4l_ls() - List directory contents
 *
 * @dirname: Directory path to list
 * Return: 0 on success, negative on error
 */
int ext4l_ls(const char *dirname);

/**
 * ext4l_exists() - Check if a file or directory exists
 *
 * @filename: Path to check
 * Return: 1 if exists, 0 if not
 */
int ext4l_exists(const char *filename);

/**
 * ext4l_size() - Get the size of a file
 *
 * @filename: Path to file
 * @sizep: Returns the file size
 * Return: 0 on success, negative on error
 */
int ext4l_size(const char *filename, loff_t *sizep);

/**
 * ext4l_read() - Read data from a file
 *
 * @filename: Path to file
 * @buf: Buffer to read data into
 * @offset: Byte offset to start reading from
 * @len: Number of bytes to read (0 = read entire file from offset)
 * @actread: Returns actual bytes read
 * Return: 0 on success, negative on error
 */
int ext4l_read(const char *filename, void *buf, loff_t offset, loff_t len,
	       loff_t *actread);

/**
 * ext4l_write() - Write data to a file
 *
 * Creates the file if it doesn't exist. Overwrites existing content.
 *
 * @filename: Path to file
 * @buf: Buffer containing data to write
 * @offset: Byte offset to start writing at
 * @len: Number of bytes to write
 * @actwrite: Returns actual bytes written
 * Return: 0 on success, -EROFS if read-only, -ENODEV if not mounted,
 *	   -ENOTDIR if parent is not a directory, negative on other errors
 */
int ext4l_write(const char *filename, void *buf, loff_t offset, loff_t len,
		loff_t *actwrite);

/**
 * ext4l_unlink() - Delete a file
 *
 * @filename: Path to file to delete
 * Return: 0 on success, -ENOENT if file not found, -EISDIR if path is a
 *	   directory, -EROFS if read-only, negative on other errors
 */
int ext4l_unlink(const char *filename);

/**
 * ext4l_mkdir() - Create a directory
 *
 * @dirname: Path of directory to create
 * Return: 0 on success, -EEXIST if directory already exists,
 *	   -ENOTDIR if parent is not a directory, -EROFS if read-only,
 *	   negative on other errors
 */
int ext4l_mkdir(const char *dirname);

/**
 * ext4l_ln() - Create a symbolic link
 *
 * Creates the symlink, replacing any existing file (like ln -sf).
 * Refuses to replace a directory.
 *
 * @filename: Path of symlink to create
 * @target: Target path the symlink points to
 * Return: 0 on success, -EISDIR if target is a directory,
 *	   -ENOTDIR if parent is not a directory, -EROFS if read-only,
 *	   negative on other errors
 */
int ext4l_ln(const char *filename, const char *target);

/**
 * ext4l_rename() - Rename a file or directory
 *
 * @old_path: Current path of file or directory
 * @new_path: New path for file or directory
 * Return: 0 on success, -ENOENT if source not found,
 *	   -ENOTDIR if parent is not a directory, -EROFS if read-only,
 *	   negative on other errors
 */
int ext4l_rename(const char *old_path, const char *new_path);

/**
 * ext4l_get_uuid() - Get the filesystem UUID
 *
 * @uuid: Buffer to receive the 16-byte UUID
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_get_uuid(u8 *uuid);

/**
 * ext4l_uuid() - Get the filesystem UUID as a string
 *
 * @uuid_str: Buffer to receive the UUID string (must be at least 37 bytes)
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_uuid(char *uuid_str);

/**
 * ext4l_statfs() - Get filesystem statistics
 *
 * @stats: Pointer to fs_statfs structure to fill
 * Return: 0 on success, -ENODEV if not mounted
 */
int ext4l_statfs(struct fs_statfs *stats);

/**
 * ext4l_opendir() - Open a directory for iteration
 *
 * @filename: Directory path
 * @dirsp: Returns directory stream pointer
 * Return: 0 on success, -ENODEV if not mounted, -ENOTDIR if not a directory,
 *	   -ENOMEM on allocation failure
 */
int ext4l_opendir(const char *filename, struct fs_dir_stream **dirsp);

/**
 * ext4l_readdir() - Read the next directory entry
 *
 * @dirs: Directory stream from ext4l_opendir
 * @dentp: Returns pointer to directory entry
 * Return: 0 on success, -ENODEV if not mounted, -ENOENT at end of directory
 */
int ext4l_readdir(struct fs_dir_stream *dirs, struct fs_dirent **dentp);

/**
 * ext4l_closedir() - Close a directory stream
 *
 * @dirs: Directory stream to close
 */
void ext4l_closedir(struct fs_dir_stream *dirs);

#endif /* __EXT4L_H__ */
