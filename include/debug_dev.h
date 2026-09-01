/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Devices which can show debug output
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __DEBUG_DEV_H
#define __DEBUG_DEV_H

#include <linux/types.h>

struct udevice;

/**
 * struct dm_debug_ops - operations for the debug uclass
 *
 * A debug device provides a way of getting a message out of U-Boot which does
 * not rely on the console. It is intended to be called explicitly from code
 * being debugged, not wired up as a console, so that it works even when the
 * console is unavailable or is the thing being investigated.
 */
struct dm_debug_ops {
	/**
	 * @puts: Write a string to the device
	 *
	 * @puts.dev: Debug device
	 * @puts.str: String to write; it need not be nul-terminated
	 * @puts.len: Number of characters to write
	 * @puts.Return: number of characters written, which may be fewer than
	 * requested, or -ve on error
	 */
	int (*puts)(struct udevice *dev, const char *str, size_t len);
};

/**
 * debug_dev_puts() - Write a string to a debug device
 *
 * All of the string is written, making as many calls to the device as are
 * needed.
 *
 * @dev: Debug device
 * @str: Nul-terminated string to write
 * Return: 0 if OK, -ve on error
 */
int debug_dev_puts(struct udevice *dev, const char *str);

/**
 * debug_dev_first() - Find the first available debug device
 *
 * This is for the common case where there is only one, so that callers need
 * not know anything about which device is in use.
 *
 * @devp: Returns the device, on success
 * Return: 0 if OK, -ENODEV if there is none, other -ve on error
 */
int debug_dev_first(struct udevice **devp);

/**
 * debug_puts() - Write a string to the first available debug device
 *
 * This is the easiest way to get a message out while debugging. It is
 * deliberately quiet about failure, since there is generally nowhere useful to
 * report it to.
 *
 * @str: Nul-terminated string to write
 * Return: 0 if OK, -ve on error
 */
int debug_puts(const char *str);

#endif /* __DEBUG_DEV_H */
