// SPDX-License-Identifier: GPL-2.0+
/*
 * Internal support functions for ext4l filesystem
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * This provides internal support functions for the ext4l driver,
 * including CRC32C, message buffer, inode allocation and folio management.
 */

#include <blk.h>
#include <dm.h>
#include <membuf.h>
#include <part.h>
#include <malloc.h>
#include <u-boot/crc.h>
#include <linux/errno.h>
#include <linux/types.h>

#include "ext4_uboot.h"
#include "ext4.h"

/* Message buffer size */
#define EXT4L_MSG_BUF_SIZE	4096

/* Message recording buffer */
static struct membuf ext4l_msg_buf;
static char ext4l_msg_data[EXT4L_MSG_BUF_SIZE];

/*
 * Global task_struct for U-Boot.
 * This must be a single global instance shared across all translation units,
 * so that journal_info remains consistent.
 */
struct task_struct ext4l_current_task = { .comm = "u-boot", .pid = 1 };

/*
 * CRC32C support - uses Castagnoli polynomial 0x82F63B78
 * Table is initialised on first mount
 */
static u32 ext4l_crc32c_table[256];
static bool ext4l_crc32c_inited;

void ext4l_crc32c_init(void)
{
	if (!ext4l_crc32c_inited) {
		crc32c_init(ext4l_crc32c_table, 0x82F63B78);
		ext4l_crc32c_inited = true;
	}
}

u32 ext4l_crc32c(u32 crc, const void *address, unsigned int length)
{
	return crc32c_cal(crc, address, length, ext4l_crc32c_table);
}

/**
 * ext4l_msg_init() - Initialise the message buffer
 */
void ext4l_msg_init(void)
{
	membuf_init(&ext4l_msg_buf, ext4l_msg_data, EXT4L_MSG_BUF_SIZE);
}

/**
 * ext4l_record_msg() - Record a message in the buffer
 *
 * @msg: Message string to record
 * @len: Length of message
 */
void ext4l_record_msg(const char *msg, int len)
{
	membuf_put(&ext4l_msg_buf, msg, len);
}

/**
 * ext4l_get_msg_buf() - Get the message buffer
 *
 * Return: Pointer to the message buffer
 */
struct membuf *ext4l_get_msg_buf(void)
{
	return &ext4l_msg_buf;
}

/**
 * ext4l_print_msgs() - Print all recorded messages
 *
 * Prints the contents of the message buffer to the console.
 */
void ext4l_print_msgs(void)
{
	char *data;
	int len;

	while ((len = membuf_getraw(&ext4l_msg_buf, 80, true, &data)) > 0)
		printf("%.*s", len, data);
}

/*
 * iget_locked - allocate a new inode
 * @sb: super block of filesystem
 * @ino: inode number to allocate
 *
 * U-Boot implementation: allocates ext4_inode_info and returns the embedded
 * vfs_inode. In Linux, this would look up the inode in a hash table first.
 * Since U-Boot is single-threaded and doesn't cache inodes, we always allocate.
 */
struct inode *iget_locked(struct super_block *sb, unsigned long ino)
{
	struct ext4_inode_info *ei;
	struct inode *inode;

	ei = kzalloc(sizeof(struct ext4_inode_info), GFP_KERNEL);
	if (!ei)
		return NULL;

	/* Get pointer to the embedded vfs_inode using offsetof */
	inode = (struct inode *)((char *)ei +
				 offsetof(struct ext4_inode_info, vfs_inode));
	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_ino = ino;
	inode->i_state = I_NEW;
	inode->i_count.counter = 1;
	inode->i_mapping = &inode->i_data;
	inode->i_data.host = inode;
	INIT_LIST_HEAD(&ei->i_es_list);
	INIT_LIST_HEAD(&inode->i_sb_list);

	/* Add to superblock's inode list for eviction on unmount */
	list_add(&inode->i_sb_list, &sb->s_inodes);

	return inode;
}

/*
 * new_inode - allocate a new empty inode
 * @sb: super block of filesystem
 *
 * U-Boot implementation: allocates ext4_inode_info for a new inode that
 * will be initialised by the caller (e.g., for creating new files).
 */
struct inode *new_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;
	struct inode *inode;

	ei = kzalloc(sizeof(struct ext4_inode_info), GFP_KERNEL);
	if (!ei)
		return NULL;

	inode = &ei->vfs_inode;
	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_nlink = 1;
	inode->i_count.counter = 1;
	inode->i_mapping = &inode->i_data;
	inode->i_data.host = inode;
	INIT_LIST_HEAD(&ei->i_es_list);
	INIT_LIST_HEAD(&inode->i_sb_list);

	/* Add to superblock's inode list for eviction on unmount */
	list_add(&inode->i_sb_list, &sb->s_inodes);

	return inode;
}

/*
 * ext4_uboot_bmap - map a logical block to a physical block
 * @inode: inode to map
 * @block: on entry, logical block number; on exit, physical block number
 *
 * U-Boot implementation of bmap for ext4. Maps a logical block number
 * to the corresponding physical block on disk.
 */
int ext4_uboot_bmap(struct inode *inode, sector_t *block)
{
	struct ext4_map_blocks map;
	int ret;

	map.m_lblk = *block;
	map.m_len = 1;
	map.m_flags = 0;

	ret = ext4_map_blocks(NULL, inode, &map, 0);
	if (ret > 0) {
		*block = map.m_pblk;
		return 0;
	}

	return ret < 0 ? ret : -EINVAL;
}

/*
 * bmap - map a logical block to a physical block (VFS interface)
 * @inode: inode to map
 * @blockp: pointer to logical block number; updated to physical block number
 *
 * This is the VFS bmap interface used by jbd2.
 */
int bmap(struct inode *inode, sector_t *blockp)
{
	return ext4_uboot_bmap(inode, blockp);
}

/* Buffer cache (bh_cache_*) is now in fs/linux_fs.c */

/* Buffer head allocation (alloc_buffer_head, free_buffer_head) is now in fs/linux_fs.c */

/* Block I/O (ext4l_read/write_block, sb_getblk, sb_bread, etc.) is now in fs/linux_fs.c */

/**
 * __filemap_get_folio() - Get or create a folio for a mapping
 * @mapping: The address_space to search
 * @index: The page index
 * @fgp_flags: Flags (FGP_CREAT to create if not found)
 * @gfp: Memory allocation flags
 * Return: Folio pointer or ERR_PTR on error
 */
struct folio *__filemap_get_folio(struct address_space *mapping,
				  pgoff_t index, unsigned int fgp_flags,
				  gfp_t gfp)
{
	struct folio *folio;
	int i;

	/* Search for existing folio in cache */
	if (mapping) {
		for (i = 0; i < mapping->folio_cache_count; i++) {
			folio = mapping->folio_cache[i];
			if (folio && folio->index == index) {
				/* Found existing folio, bump refcount */
				folio->_refcount++;
				return folio;
			}
		}
	}

	/* If not creating, return error */
	if (!(fgp_flags & FGP_CREAT))
		return ERR_PTR(-ENOENT);

	/* Create new folio */
	folio = kzalloc(sizeof(struct folio), gfp);
	if (!folio)
		return ERR_PTR(-ENOMEM);

	folio->data = kzalloc(PAGE_SIZE, gfp);
	if (!folio->data) {
		kfree(folio);
		return ERR_PTR(-ENOMEM);
	}

	folio->index = index;
	folio->mapping = mapping;
	folio->_refcount = 1;

	/* Add to cache if there's room */
	if (mapping && mapping->folio_cache_count < FOLIO_CACHE_MAX) {
		mapping->folio_cache[mapping->folio_cache_count++] = folio;
		/* Extra ref for cache */
		folio->_refcount++;
	}

	return folio;
}

/**
 * folio_put() - Release a reference to a folio
 * @folio: The folio to release
 */
void folio_put(struct folio *folio)
{
	if (!folio)
		return;
	if (--folio->_refcount > 0)
		return;
	kfree(folio->data);
	kfree(folio);
}

/**
 * folio_get() - Acquire a reference to a folio
 * @folio: The folio to reference
 */
void folio_get(struct folio *folio)
{
	if (folio)
		folio->_refcount++;
}

/**
 * mapping_clear_folio_cache() - Release all folios in an address_space cache
 * @mapping: The address_space to clear
 *
 * Releases the cache's reference to each folio. If no other references exist,
 * the folio will be freed.
 */
void mapping_clear_folio_cache(struct address_space *mapping)
{
	int i;

	if (!mapping)
		return;

	for (i = 0; i < mapping->folio_cache_count; i++) {
		struct folio *folio = mapping->folio_cache[i];

		if (folio)
			folio_put(folio);
		mapping->folio_cache[i] = NULL;
	}
	mapping->folio_cache_count = 0;
}
