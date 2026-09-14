// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_RAM

#include <display_options.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <mapmem.h>
#include <ram.h>
#include <time.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <linux/string.h>

int ram_get_info(struct udevice *dev, struct ram_info *info)
{
	struct ram_ops *ops = ram_get_ops(dev);

	if (!ops->get_info)
		return -ENOSYS;

	return ops->get_info(dev, info);
}

int ram_clear_all(void)
{
	struct udevice *dev;
	ulong total = 0;
	ulong start;

	start = get_timer(0);
	uclass_foreach_dev_probe(UCLASS_RAM, dev) {
		struct ram_info info;
		void *ptr;
		int ret;

		ret = ram_get_info(dev, &info);
		if (ret)
			return log_msg_ret("inf", ret);
		ptr = map_sysmem(info.base, info.size);
		memset(ptr, '\0', info.size);
		unmap_sysmem(ptr);
		total += info.size;
	}
	printf("Cleared ");
	print_size(total, " of RAM in ");
	printf("%lu ms\n", get_timer(start));

	return 0;
}

UCLASS_DRIVER(ram) = {
	.id		= UCLASS_RAM,
	.name		= "ram",
};
