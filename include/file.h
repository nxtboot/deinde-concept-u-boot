/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Implementation of files on a filesystem
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#ifndef __FILE_H
#define __FILE_H

#include <dir.h>
#include <iovec.h>

struct udevice;

enum {
	/* Maximum length of a pathname */
	FILE_MAX_PATH_LEN		= 1024,
};

/**
 * struct file_uc_priv - Uclass information about each file
 *
 * @leaf: Filename leaf
 * @pos: Current file position
 * @size: File size
 */
struct file_uc_priv {
	const char *leaf;
	loff_t pos;
	size_t size;
};

struct file_ops {
	/**
	 * read_iter() - Read data from a file
	 *
	 * Reads from the current file position, which is advanced
	 *
	 * @dev: File to read from
	 * @iter: Interator to receive data
	 * @pos: File position to read from
	 * Return: number of bytes read, or -ve error code
	 */
	ssize_t (*read_iter)(struct udevice *dev, struct iov_iter *iter,
			     loff_t pos);

	/**
	 * write_iter() - Write data to a file
	 *
	 * @dev: File to write to
	 * @iter: Iterator providing the data to write
	 * @pos: File position to write at
	 * Return: number of bytes written, or -ve error code
	 */
	ssize_t (*write_iter)(struct udevice *dev, struct iov_iter *iter,
			      loff_t pos);
};

/* Get access to a file's operations */
#define file_get_ops(dev)		((struct file_ops *)(dev)->driver->ops)

/**
 * file_read() - Read data from a file
 *
 * Reads from the current file position, which is advanced
 *
 * @dev: File to read from
 * @buf: Buffer to read into
 * @len: Number of bytes to read
 */
long file_read(struct udevice *dev, void *buf, long len);

/**
 * read_at() - Read data from a file at a particular position
 *
 * @dev: File to read from
 * @buf: Buffer to read into
 * @offset: Offset within the file to start reading
 * @len: Number of bytes to read (0 to read as many as possible)
 */
long file_read_at(struct udevice *dev, void *buf, loff_t offset, long len);

/**
 * file_write_at() - Write data to a file at a particular position
 *
 * @dev: File to write to
 * @buf: Buffer containing data to write
 * @offset: Offset within the file to start writing
 * @len: Number of bytes to write
 * Return: number of bytes written, or -ve error code
 */
long file_write_at(struct udevice *dev, const void *buf, loff_t offset,
		   long len);

/**
 * file_add_probe() - Create a new file device for a file
 *
 * @dev: Directory device (UCLASS_DIR)
 * @leaf: Filename within the directory
 * @size: Size of the file in bytes
 * @flags: Open-mode flags to use
 * @filp: Returns the UCLASS_FILE device
 * Return: 0 if OK, -ve on error
 */
int file_add_probe(struct udevice *dir, struct driver *drv, const char *leaf,
		   size_t size, enum dir_open_flags_t flags,
		   struct udevice **devp);

#endif
