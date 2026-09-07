// SPDX-License-Identifier: GPL-2.0+
/*
 * Devices which can show debug output
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_DEBUG

#include <debug_dev.h>
#include <dm.h>
#include <event.h>
#include <vsprintf.h>
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

static int debug_post_probe(struct udevice *dev)
{
	char msg[40];

	if (!CONFIG_IS_ENABLED(DEBUG_DEV_ANNOUNCE))
		return 0;

	snprintf(msg, sizeof(msg), "U-Boot debug device %s\n", dev->name);

	/*
	 * Ignore any error, since there is nowhere useful to report it to and
	 * the device should still be usable
	 */
	debug_dev_puts(dev, msg);

	return 0;
}

/*
 * Probe every debug device, so that each one announces itself. This is left
 * until the end of init, since a debug device may sit on a bus which is not
 * usable before then: probing it earlier can leave that bus in a state which
 * stops it working at all
 */
static int debug_announce(void)
{
	struct udevice *dev;

	if (!CONFIG_IS_ENABLED(DEBUG_DEV_ANNOUNCE))
		return 0;

	uclass_foreach_dev_probe(UCLASS_DEBUG, dev) {
		/* the announcement is made by debug_post_probe() */
	}

	return 0;
}
EVENT_SPY_SIMPLE(EVT_LAST_STAGE_INIT, debug_announce);

UCLASS_DRIVER(debug) = {
	.name	= "debug",
	.id	= UCLASS_DEBUG,
	.post_probe = debug_post_probe,
};
