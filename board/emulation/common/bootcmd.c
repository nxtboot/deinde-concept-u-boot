// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <errno.h>
#include <event.h>
#include <qfw.h>

#if IS_ENABLED(CONFIG_EVENT)
static int qemu_get_bootcmd(void *ctx, struct event *event)
{
	struct event_bootcmd *bc = &event->data.bootcmd;
	enum fw_cfg_selector select;
	struct udevice *qfw_dev;
	ulong size;

	if (qfw_get_dev(&qfw_dev))
		return 0;

	if (qfw_locate_file(qfw_dev, "opt/u-boot/bootcmd", &select, &size))
		return 0;
	if (!size)
		return 0;

	/* Check if the command fits in the provided buffer with terminator */
	if (size >= bc->size)
		return -ENOSPC;

	qfw_read_entry(qfw_dev, select, size, bc->bootcmd);
	bc->bootcmd[size] = '\0';

	return 0;
}
EVENT_SPY_FULL(EVT_BOOTCMD, qemu_get_bootcmd);
#endif
