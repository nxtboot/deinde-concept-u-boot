/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal VFS helpers - not part of the public API
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __VFS_INTERNAL_H
#define __VFS_INTERNAL_H

struct udevice;

/**
 * struct vfs_priv - VFS root FS device driver-private data
 *
 * @mount_count: Counter for unique mount device names
 */
struct vfs_priv {
	int mount_count;
};

#endif
