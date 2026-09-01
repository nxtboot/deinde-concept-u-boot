// SPDX-License-Identifier: GPL-2.0+
/*
 * Devices which can show debug output
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_DEBUG

#include <debug_dev.h>
#include <dm.h>
#include <linux/errno.h>

int debug_dev_puts(struct udevice *dev, const char *str)
{
	const struct dm_debug_ops *ops = device_get_ops(dev);
	size_t len = strlen(str);
	size_t done = 0;

	if (!ops->puts)
		return -ENOSYS;

	while (done < len) {
		int ret;

		ret = ops->puts(dev, str + done, len - done);
		if (ret < 0)
			return ret;
		if (!ret)
			return -EIO;
		done += ret;
	}

	return 0;
}

int debug_dev_first(struct udevice **devp)
{
	return uclass_first_device_err(UCLASS_DEBUG, devp);
}

int debug_puts(const char *str)
{
	struct udevice *dev;
	int ret;

	ret = debug_dev_first(&dev);
	if (ret)
		return ret;

	return debug_dev_puts(dev, str);
}

UCLASS_DRIVER(debug) = {
	.name	= "debug",
	.id	= UCLASS_DEBUG,
};
