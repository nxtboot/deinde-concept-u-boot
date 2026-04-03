// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Filesystem layer
 *
 * Manages a mount table using UCLASS_MOUNT devices, providing unified
 * path resolution across mounted filesystems. Inspired by the Linux VFS.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <dm.h>
#include <vfs.h>

UCLASS_DRIVER(mount) = {
	.name			= "mount",
	.id			= UCLASS_MOUNT,
	.per_device_auto	= sizeof(struct vfsmount),
};
