/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __CBFS_FS__
#define __CBFS_FS__

struct blk_desc;
struct disk_partition;

int cbfs_fs_set_blk_dev(struct blk_desc *rbdd, struct disk_partition *info);
int cbfs_fs_exists(const char *filename);
int cbfs_fs_size(const char *filename, loff_t *size);
int cbfs_fs_read(const char *filename, void *buf, loff_t offset, loff_t len,
		 loff_t *actread);
int cbfs_fs_ls(const char *dirname);

#endif
