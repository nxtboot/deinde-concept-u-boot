// SPDX-License-Identifier: GPL-2.0+
/*
 * The legacy-filesystem view of CBFS, so that the generic file commands can
 * reach it
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <cbfs.h>
#include <cbfsfs.h>
#include <fs.h>
#include <linux/errno.h>

int cbfs_fs_set_blk_dev(struct blk_desc *rbdd, struct disk_partition *info)
{
	/*
	 * CBFS sits at a fixed place in the ROM rather than on a block device,
	 * so only the null device is accepted. The 'cbfsinit' command says
	 * where it is and must have been run first.
	 */
	if (rbdd)
		return -ENODEV;
	if (!file_cbfs_get_header())
		return -ENOENT;

	return 0;
}

int cbfs_fs_exists(const char *filename)
{
	return file_cbfs_find(filename) ? 1 : 0;
}

int cbfs_fs_size(const char *filename, loff_t *size)
{
	const struct cbfs_cachenode *file;

	file = file_cbfs_find(filename);
	if (!file)
		return -ENOENT;
	*size = file_cbfs_size(file);

	return 0;
}

int cbfs_fs_read(const char *filename, void *buf, loff_t offset, loff_t len,
		 loff_t *actread)
{
	const struct cbfs_cachenode *file;
	u32 size;

	file = file_cbfs_find(filename);
	if (!file)
		return -ENOENT;

	size = file_cbfs_size(file);
	if (offset > size)
		return -EINVAL;
	size -= offset;
	if (len && len < size)
		size = len;

	memcpy(buf, file->data + offset, size);
	*actread = size;

	return 0;
}

int cbfs_fs_ls(const char *dirname)
{
	const struct cbfs_cachenode *file;
	int files = 0;

	for (file = file_cbfs_get_first(); file; file_cbfs_get_next(&file)) {
		printf("%8d %s\n", file_cbfs_size(file), file_cbfs_name(file));
		files++;
	}
	printf("\n%d file(s)\n", files);

	return 0;
}
