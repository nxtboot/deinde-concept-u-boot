/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Public interface for the isofs (ISO 9660) filesystem driver
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __ISOFS_H__
#define __ISOFS_H__

#include <blk.h>
#include <part.h>
#include <fs.h>

/**
 * isofs_probe() - Mount an ISO 9660 filesystem
 *
 * @fs_dev_desc: Block device descriptor
 * @fs_partition: Partition info (can be NULL for whole disk)
 * Return: 0 on success, negative on error
 */
int isofs_probe(struct blk_desc *fs_dev_desc,
		struct disk_partition *fs_partition);

/**
 * isofs_close() - Unmount the ISO 9660 filesystem
 */
void isofs_close(void);

/**
 * isofs_ls() - List files in a directory
 *
 * @dirname: Directory path
 * Return: 0 on success, negative on error
 */
int isofs_ls(const char *dirname);

/**
 * isofs_exists() - Check if a file exists
 *
 * @filename: File path
 * Return: 1 if file exists, 0 if not
 */
int isofs_exists(const char *filename);

/**
 * isofs_size() - Get the size of a file
 *
 * @filename: File path
 * @sizep: Output file size
 * Return: 0 on success, negative on error
 */
int isofs_size(const char *filename, loff_t *sizep);

/**
 * isofs_read() - Read data from a file
 *
 * @filename: File path
 * @buf: Output buffer
 * @offset: Byte offset to read from
 * @len: Number of bytes to read (0 = entire file)
 * @actread: Output actual bytes read
 * Return: 0 on success, negative on error
 */
int isofs_read(const char *filename, void *buf, loff_t offset, loff_t len,
	       loff_t *actread);

/**
 * isofs_opendir() - Open a directory for iteration
 *
 * @filename: Directory path
 * @dirsp: Output directory stream pointer
 * Return: 0 on success, negative on error
 */
int isofs_opendir(const char *filename, struct fs_dir_stream **dirsp);

/**
 * isofs_readdir() - Read next directory entry
 *
 * @dirs: Directory stream from isofs_opendir()
 * @dentp: Output directory entry pointer
 * Return: 0 on success, -ENOENT at end, negative on error
 */
int isofs_readdir(struct fs_dir_stream *dirs, struct fs_dirent **dentp);

/**
 * isofs_closedir() - Close a directory stream
 *
 * @dirs: Directory stream to close
 */
void isofs_closedir(struct fs_dir_stream *dirs);

#endif /* __ISOFS_H__ */
