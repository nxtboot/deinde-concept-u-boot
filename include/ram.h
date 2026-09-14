/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __RAM_H
#define __RAM_H

#include <linux/types.h>

struct udevice;

struct ram_info {
	phys_addr_t base;
	size_t size;
};

struct ram_ops {
	/**
	 * get_info() - Get basic memory info
	 *
	 * @dev:	Device to check (UCLASS_RAM)
	 * @info:	Place to put info
	 * @return 0 if OK, -ve on error
	 */
	int (*get_info)(struct udevice *dev, struct ram_info *info);
};

#define ram_get_ops(dev)        ((struct ram_ops *)(dev)->driver->ops)

/**
 * ram_get_info() - Get information about a RAM device
 *
 * @dev:	Device to check (UCLASS_RAM)
 * @info:	Returns RAM info
 * Return: 0 if OK, -ve on error
 */
int ram_get_info(struct udevice *dev, struct ram_info *info);

/**
 * ram_clear_all() - Zero the whole of RAM
 *
 * This probes each RAM device and clears the region it reports. It is for use
 * in the phase which sets up RAM, before anything is loaded into it, so that
 * nothing left behind by a previous OS survives into the next one. The amount
 * cleared and the time taken are printed.
 *
 * Return: 0 if OK, -ve if a RAM device cannot be probed or cannot report its
 * region
 */
int ram_clear_all(void);

#endif
