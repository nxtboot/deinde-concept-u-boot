// SPDX-License-Identifier: GPL-2.0+
/*
 * Common Linux VFS compatibility functions for U-Boot
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Shared functions used by Linux-ported filesystem drivers (ext4l, isofs).
 * These implement parts of the Linux VFS layer needed by multiple drivers,
 * including the buffer cache and block I/O.
 */

/*
 * Suppress warnings for unused static functions from included Linux headers
 */
#pragma GCC diagnostic ignored "-Wunused-function"

#include <blk.h>
#include <memalign.h>
#include <malloc.h>
#include <part.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <asm/atomic.h>

#if IS_ENABLED(CONFIG_FS_EXT4L)
#include "../fs/ext4l/ext4_uboot.h"
#include "../fs/ext4l/ext4.h"
#else
/*
 * Standalone definitions for when ext4l is not compiled.
 * These provide the buffer head private bits and JBD stubs.
 */
#define BH_OwnsData		(BH_PrivateStart + 1)
BUFFER_FNS(OwnsData, ownsdata)
#define BH_Cached		(BH_PrivateStart + 2)
BUFFER_FNS(Cached, cached)

/* JBD stubs when ext4l is not compiled */
#define buffer_jbd(bh)		(0)
#define clear_buffer_jbd(bh)	do { } while (0)

struct journal_head;
static inline struct journal_head *bh2jh(struct buffer_head *bh)
{
	return NULL;
}
#endif /* CONFIG_FS_EXT4L */

/**
 * linux_fs_read_block() - Read a block from the block device
 * @sb: Super block (provides block device and partition info)
 * @block: Block number (filesystem block, not sector)
 * @size: Block size in bytes
 * @buffer: Destination buffer
 * Return: 0 on success, negative on error
 */
static int linux_fs_read_block(struct super_block *sb, sector_t block,
			       size_t size, void *buffer)
{
	struct udevice *blk_dev = sb->s_bdev->bd_blk;
	unsigned long part_start = sb->s_bdev->bd_part_start;
	lbaint_t sector, sector_count;
	long n;

	if (!blk_dev)
		return -EIO;

	sector = (block * size) / SECTOR_SIZE + part_start;
	sector_count = size / SECTOR_SIZE;
	if (sector_count == 0)
		sector_count = 1;

	n = blk_read(blk_dev, sector, sector_count, buffer);
	if (n != sector_count)
		return -EIO;

	return 0;
}

/**
 * linux_fs_write_block() - Write a block to the block device
 * @sb: Super block (provides block device and partition info)
 * @block: Block number (filesystem block, not sector)
 * @size: Block size in bytes
 * @buffer: Source buffer
 * Return: 0 on success, negative on error
 */
static int linux_fs_write_block(struct super_block *sb, sector_t block,
				size_t size, void *buffer)
{
	struct udevice *blk_dev = sb->s_bdev->bd_blk;
	unsigned long part_start = sb->s_bdev->bd_part_start;
	lbaint_t sector, sector_count;
	long n;

	if (!blk_dev)
		return -EIO;

	sector = (block * size) / SECTOR_SIZE + part_start;
	sector_count = size / SECTOR_SIZE;
	if (sector_count == 0)
		sector_count = 1;

	n = blk_write(blk_dev, sector, sector_count, buffer);
	if (n != sector_count)
		return -EIO;

	return 0;
}

/*
 * Buffer cache
 *
 * Linux's sb_getblk() returns the same buffer_head for the same block number,
 * allowing flags like BH_Verified, BH_Uptodate, etc. to persist across calls.
 * This is critical for ext4's bitmap validation which sets buffer_verified()
 * and expects it to remain set on subsequent lookups.
 */
#define BH_CACHE_BITS	8
#define BH_CACHE_SIZE	(1 << BH_CACHE_BITS)
#define BH_CACHE_MASK	(BH_CACHE_SIZE - 1)

struct bh_cache_entry {
	struct buffer_head *bh;
	struct bh_cache_entry *next;
};

static struct bh_cache_entry *bh_cache[BH_CACHE_SIZE];

static inline unsigned int bh_cache_hash(sector_t block)
{
	return (unsigned int)(block & BH_CACHE_MASK);
}

/**
 * bh_cache_lookup() - Look up a buffer in the cache
 * @bdev: Block device to match
 * @block: Block number to look up
 * @size: Expected block size
 * Return: Buffer head if found with matching device and size, NULL otherwise
 */
static struct buffer_head *bh_cache_lookup(struct block_device *bdev,
					   sector_t block, size_t size)
{
	unsigned int hash = bh_cache_hash(block);
	struct bh_cache_entry *entry;

	for (entry = bh_cache[hash]; entry; entry = entry->next) {
		if (entry->bh && entry->bh->b_bdev == bdev &&
		    entry->bh->b_blocknr == block &&
		    entry->bh->b_size == size) {
			atomic_inc(&entry->bh->b_count);
			return entry->bh;
		}
	}
	return NULL;
}

/**
 * bh_cache_insert() - Insert a buffer into the cache
 * @bh: Buffer head to insert
 */
static void bh_cache_insert(struct buffer_head *bh)
{
	unsigned int hash = bh_cache_hash(bh->b_blocknr);
	struct bh_cache_entry *entry;

	/* Check if already in cache - must match device, block AND size */
	for (entry = bh_cache[hash]; entry; entry = entry->next) {
		if (entry->bh && entry->bh->b_bdev == bh->b_bdev &&
		    entry->bh->b_blocknr == bh->b_blocknr &&
		    entry->bh->b_size == bh->b_size)
			return;
	}

	entry = malloc(sizeof(struct bh_cache_entry));
	if (!entry)
		return;

	entry->bh = bh;
	entry->next = bh_cache[hash];
	bh_cache[hash] = entry;

	set_buffer_cached(bh);
	atomic_inc(&bh->b_count);
}

/**
 * bh_clear_stale_jbd() - Clear stale journal_head from a buffer_head
 * @bh: buffer_head to check
 */
static void bh_clear_stale_jbd(struct buffer_head *bh)
{
	if (buffer_jbd(bh)) {
		clear_buffer_jbd(bh);
		bh->b_private = NULL;
	}
}

/**
 * bh_cache_clear() - Clear cached buffers for a block device
 * @bdev: Block device whose buffers should be freed
 *
 * Called on unmount to free buffers belonging to @bdev only, leaving
 * buffers from other mounted filesystems intact.
 */
void bh_cache_clear(struct block_device *bdev)
{
	int i;
	struct bh_cache_entry *entry, *next, **prev;

	for (i = 0; i < BH_CACHE_SIZE; i++) {
		prev = &bh_cache[i];
		for (entry = *prev; entry; entry = next) {
			next = entry->next;
			if (entry->bh && entry->bh->b_bdev == bdev) {
				struct buffer_head *bh = entry->bh;

				bh_clear_stale_jbd(bh);
				atomic_set(&bh->b_count, 1);
				if (atomic_dec_and_test(&bh->b_count))
					free_buffer_head(bh);
				*prev = next;
				free(entry);
			} else {
				prev = &entry->next;
			}
		}
	}
}

/**
 * bh_cache_release_jbd() - Release JBD references from cached buffers
 * @bdev: Block device whose buffers should be processed
 *
 * Must be called after journal destroy but before bh_cache_clear().
 */
void bh_cache_release_jbd(struct block_device *bdev)
{
	int i;
	struct bh_cache_entry *entry;

	for (i = 0; i < BH_CACHE_SIZE; i++) {
		for (entry = bh_cache[i]; entry; entry = entry->next) {
			if (entry->bh && entry->bh->b_bdev == bdev &&
			    buffer_jbd(entry->bh)) {
				struct buffer_head *bh = entry->bh;
				struct journal_head *jh = bh2jh(bh);

				if (jh) {
					jh->b_bh = NULL;
					jh->b_transaction = NULL;
					jh->b_next_transaction = NULL;
					jh->b_cp_transaction = NULL;
				}
				clear_buffer_jbd(bh);
				bh->b_private = NULL;
			}
		}
	}
}

/**
 * bh_cache_sync() - Sync all dirty buffers to disk
 *
 * Return: 0 on success, negative on first error
 */
int bh_cache_sync(void)
{
	int i, ret = 0;
	struct bh_cache_entry *entry;

	for (i = 0; i < BH_CACHE_SIZE; i++) {
		for (entry = bh_cache[i]; entry; entry = entry->next) {
			if (entry->bh && buffer_dirty(entry->bh)) {
				struct buffer_head *bh = entry->bh;
				int err;

				err = linux_fs_write_block(bh->b_bdev->bd_super,
							   bh->b_blocknr,
							   bh->b_size,
							   bh->b_data);
				if (err && !ret)
					ret = err;
				clear_buffer_dirty(bh);
			}
		}
	}
	return ret;
}

/* Buffer head allocation */

/**
 * alloc_buffer_head() - Allocate a buffer_head structure
 * @gfp_mask: Allocation flags (ignored in U-Boot)
 * Return: Pointer to buffer_head or NULL on error
 */
struct buffer_head *alloc_buffer_head(gfp_t gfp_mask)
{
	struct buffer_head *bh;

	bh = kzalloc(sizeof(*bh), GFP_KERNEL);
	if (!bh)
		return NULL;

	atomic_set(&bh->b_count, 1);
	return bh;
}

/**
 * alloc_buffer_head_with_data() - Allocate a buffer_head with data buffer
 * @size: Size of the data buffer to allocate
 * Return: Pointer to buffer_head or NULL on error
 */
static struct buffer_head *alloc_buffer_head_with_data(size_t size)
{
	struct buffer_head *bh;

	bh = kzalloc(sizeof(*bh), GFP_KERNEL);
	if (!bh)
		return NULL;

	bh->b_data = malloc(size);
	if (!bh->b_data) {
		free(bh);
		return NULL;
	}

	bh->b_size = size;
	bh->b_folio = kzalloc(sizeof(struct folio), GFP_KERNEL);
	if (bh->b_folio)
		bh->b_folio->data = bh->b_data;
	atomic_set(&bh->b_count, 1);
	set_bit(BH_OwnsData, &bh->b_state);

	return bh;
}

/**
 * free_buffer_head() - Free a buffer_head
 * @bh: Buffer head to free
 *
 * Only free b_data if BH_OwnsData is set. Shadow buffers created by
 * jbd2_journal_write_metadata_buffer() share b_data with the original.
 */
void free_buffer_head(struct buffer_head *bh)
{
	if (!bh)
		return;

	/* Don't free if journal still holds a reference */
	if (buffer_jbd(bh))
		return;

	/* Shadow buffers share their folio - don't free it */
	if (!bh->b_private && bh->b_folio)
		free(bh->b_folio);

	if (bh->b_data && test_bit(BH_OwnsData, &bh->b_state))
		free(bh->b_data);
	free(bh);
}

/* Buffer head I/O */

/**
 * sb_getblk() - Get a buffer, using cache if available
 * @sb: Super block
 * @block: Block number
 * Return: Buffer head or NULL on error
 */
struct buffer_head *sb_getblk(struct super_block *sb, sector_t block)
{
	struct buffer_head *bh;

	if (!sb)
		return NULL;

	bh = bh_cache_lookup(sb->s_bdev, block, sb->s_blocksize);
	if (bh)
		return bh;

	bh = alloc_buffer_head_with_data(sb->s_blocksize);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = sb->s_bdev;
	bh->b_size = sb->s_blocksize;
	set_buffer_mapped(bh);
	memset(bh->b_data, '\0', bh->b_size);

	bh_cache_insert(bh);

	return bh;
}

/**
 * __getblk() - Get a buffer for a given block device
 * @bdev: Block device
 * @block: Block number
 * @size: Block size
 * Return: Buffer head or NULL on error
 */
struct buffer_head *__getblk(struct block_device *bdev, sector_t block,
			     unsigned int size)
{
	struct buffer_head *bh;

	bh = bh_cache_lookup(bdev, block, size);
	if (bh)
		return bh;

	bh = alloc_buffer_head_with_data(size);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = bdev;
	bh->b_size = size;
	set_buffer_mapped(bh);
	memset(bh->b_data, '\0', bh->b_size);

	bh_cache_insert(bh);

	return bh;
}

/**
 * sb_bread() - Read a block via super_block
 * @sb: Super block
 * @block: Block number to read
 * Return: Buffer head with data, or NULL on error
 */
struct buffer_head *sb_bread(struct super_block *sb, sector_t block)
{
	struct buffer_head *bh;
	int ret;

	if (!sb)
		return NULL;

	bh = sb_getblk(sb, block);
	if (!bh)
		return NULL;

	if (buffer_uptodate(bh))
		return bh;

	bh->b_blocknr = block;
	bh->b_bdev = sb->s_bdev;
	bh->b_size = sb->s_blocksize;

	ret = linux_fs_read_block(sb, block, sb->s_blocksize, bh->b_data);
	if (ret) {
		brelse(bh);
		return NULL;
	}

	set_buffer_uptodate(bh);

	return bh;
}

/**
 * bdev_getblk() - Get buffer via block_device
 * @bdev: Block device
 * @block: Block number
 * @size: Block size
 * @gfp: GFP flags (ignored)
 * Return: Buffer head or NULL on error
 */
struct buffer_head *bdev_getblk(struct block_device *bdev, sector_t block,
				unsigned int size, gfp_t gfp)
{
	return __getblk(bdev, block, size);
}

/**
 * __bread() - Read a block via block device
 * @bdev: Block device
 * @block: Block number to read
 * @size: Block size
 * Return: Buffer head or NULL on error
 */
struct buffer_head *__bread(struct block_device *bdev, sector_t block,
			    unsigned int size)
{
	struct buffer_head *bh;
	int ret;

	bh = __getblk(bdev, block, size);
	if (!bh)
		return NULL;

	if (buffer_uptodate(bh))
		return bh;

	ret = linux_fs_read_block(bdev->bd_super, block, size, bh->b_data);
	if (ret) {
		brelse(bh);
		return NULL;
	}
	set_buffer_uptodate(bh);

	return bh;
}

/**
 * brelse() - Release a buffer_head
 * @bh: Buffer head to release
 */
void brelse(struct buffer_head *bh)
{
	if (!bh)
		return;

	/* If JBD owns this buffer, don't let ref count reach zero */
	if (buffer_jbd(bh) && atomic_read(&bh->b_count) <= 1)
		return;

	if (atomic_dec_and_test(&bh->b_count) && !buffer_cached(bh))
		free_buffer_head(bh);
}

/**
 * __brelse() - Release a buffer_head without freeing
 * @bh: Buffer head to release
 */
void __brelse(struct buffer_head *bh)
{
	if (bh)
		atomic_dec(&bh->b_count);
}

/**
 * submit_bh() - Submit a buffer_head for I/O
 * @op: Operation (REQ_OP_READ, REQ_OP_WRITE, etc.)
 * @bh: Buffer head to submit
 * Return: 0 on success, negative on error
 */
int submit_bh(int op, struct buffer_head *bh)
{
	struct super_block *sb;
	int ret = 0;
	int op_type = op & REQ_OP_MASK;
	int uptodate;

	sb = bh->b_bdev ? bh->b_bdev->bd_super : NULL;
	if (!sb)
		return -EIO;

	if (op_type == REQ_OP_READ) {
		ret = linux_fs_read_block(sb, bh->b_blocknr, bh->b_size,
					  bh->b_data);
		if (ret) {
			clear_buffer_uptodate(bh);
			uptodate = 0;
		} else {
			set_buffer_uptodate(bh);
			uptodate = 1;
		}
	} else if (op_type == REQ_OP_WRITE) {
		ret = linux_fs_write_block(sb, bh->b_blocknr, bh->b_size,
					   bh->b_data);
		if (ret) {
			clear_buffer_uptodate(bh);
			set_buffer_write_io_error(bh);
			uptodate = 0;
		} else {
			clear_buffer_write_io_error(bh);
			uptodate = 1;
		}
	} else {
		uptodate = 0;
	}

	if (bh->b_end_io)
		bh->b_end_io(bh, uptodate);

	return ret;
}

/**
 * bh_read() - Read a buffer_head from disk
 * @bh: Buffer head to read
 * @flags: Read flags
 * Return: 0 on success, negative on error
 */
int bh_read(struct buffer_head *bh, int flags)
{
	if (!bh || !bh->b_data)
		return -EINVAL;

	submit_bh(REQ_OP_READ | flags, bh);
	return buffer_uptodate(bh) ? 0 : -EIO;
}

/* end_buffer_write_sync - completion for sync writes */
void end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/* VFS helpers */

/**
 * dir_emit_dot() - Emit "." directory entry
 * @file: File pointer for the directory
 * @ctx: Directory context
 * Return: true if iteration should continue
 */
bool dir_emit_dot(struct file *file, struct dir_context *ctx)
{
	return dir_emit(ctx, ".", 1, file_inode(file)->i_ino, DT_DIR);
}

/**
 * dir_emit_dotdot() - Emit ".." directory entry
 * @file: File pointer for the directory
 * @ctx: Directory context
 * Return: true if iteration should continue
 */
bool dir_emit_dotdot(struct file *file, struct dir_context *ctx)
{
	return dir_emit(ctx, "..", 2, 0, DT_DIR);
}

ssize_t generic_read_dir(struct file *f, char __user *buf, size_t count,
			 loff_t *ppos)
{
	return -EISDIR;
}

int generic_check_addressable(unsigned int blocksize_bits, u64 num_blocks)
{
	if (num_blocks > (1ULL << (sizeof(sector_t) * 8 - blocksize_bits)))
		return -EFBIG;
	return 0;
}

const struct file_operations generic_ro_fops = {
};

const struct inode_operations page_symlink_inode_operations = {
};
