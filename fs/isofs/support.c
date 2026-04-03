// SPDX-License-Identifier: GPL-2.0+
/*
 * Internal support functions for isofs filesystem
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * This provides isofs-specific support functions: iget5_locked() with
 * custom test/set callbacks, and inode management.
 *
 * Common VFS functions (buffer cache, block I/O, brelse, dir_emit, etc.)
 * are provided by fs/linux_fs.c.
 */

#include <malloc.h>
#include <linux/errno.h>
#include <linux/types.h>

#include "isofs.h"

/**
 * iget5_locked() - Get an inode with custom test/set callbacks
 * @sb: Superblock
 * @hashval: Hash value (unused in U-Boot)
 * @test: Test function to check if inode matches
 * @set: Set function to initialise inode data
 * @data: Opaque data passed to test/set
 *
 * In U-Boot we always allocate a new inode since we don't cache them.
 */
struct inode *iget5_locked(struct super_block *sb, unsigned long hashval,
			   int (*test)(struct inode *, void *),
			   int (*set)(struct inode *, void *), void *data)
{
	struct iso_inode_info *ei;
	struct inode *inode;

	ei = kzalloc(sizeof(*ei), GFP_KERNEL);
	if (!ei)
		return NULL;

	inode = &ei->vfs_inode;
	memset(inode, '\0', sizeof(*inode));
	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_state = I_NEW;
	inode->i_count.counter = 1;
	inode->i_mapping = &inode->i_data;
	inode->i_data.host = inode;
	INIT_LIST_HEAD(&inode->i_sb_list);

	if (set)
		set(inode, data);

	list_add(&inode->i_sb_list, &sb->s_inodes);

	return inode;
}

/**
 * iget_failed() - Mark inode as failed and release
 * @inode: Inode that failed to initialise
 */
void iget_failed(struct inode *inode)
{
	if (!inode)
		return;
	list_del_init(&inode->i_sb_list);
	kfree(ISOFS_I(inode));
}

/**
 * isofs_free_inodes() - Free all inodes on the superblock list
 * @sb: Superblock whose inodes to free
 */
void isofs_free_inodes(struct super_block *sb)
{
	while (!list_empty(&sb->s_inodes)) {
		struct inode *inode;

		inode = list_first_entry(&sb->s_inodes,
					 struct inode, i_sb_list);
		list_del_init(&inode->i_sb_list);
		kfree(ISOFS_I(inode));
	}
}
