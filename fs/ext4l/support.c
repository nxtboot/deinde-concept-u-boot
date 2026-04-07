// SPDX-License-Identifier: GPL-2.0+
/*
 * Internal support functions for ext4l filesystem
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * This provides internal support functions for the ext4l driver,
 * including buffer_head I/O and buffer cache.
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

/*
 * Buffer cache implementation
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
 * @block: Block number to look up
 * @size: Expected block size
 * Return: Buffer head if found with matching size, NULL otherwise
 */
static struct buffer_head *bh_cache_lookup(sector_t block, size_t size)
{
	unsigned int hash = bh_cache_hash(block);
	struct bh_cache_entry *entry;

	for (entry = bh_cache[hash]; entry; entry = entry->next) {
		if (entry->bh && entry->bh->b_blocknr == block &&
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

	/* Check if already in cache - must match block AND size */
	for (entry = bh_cache[hash]; entry; entry = entry->next) {
		if (entry->bh && entry->bh->b_blocknr == bh->b_blocknr &&
		    entry->bh->b_size == bh->b_size)
			return;  /* Already cached */
	}

	entry = malloc(sizeof(struct bh_cache_entry));
	if (!entry)
		return;  /* Silently fail - cache is optional */

	entry->bh = bh;
	entry->next = bh_cache[hash];
	bh_cache[hash] = entry;

	/* Mark as cached so brelse() knows not to free it */
	set_buffer_cached(bh);

	/* Add a reference to keep the buffer alive in cache */
	atomic_inc(&bh->b_count);
}

/**
 * bh_cache_clear() - Clear the entire buffer cache
 *
 * Called on unmount to free all cached buffers.
 */
/**
 * bh_clear_stale_jbd() - Clear stale journal_head from buffer_head
 * @bh: buffer_head to check
 *
 * Check if the buffer still has journal_head attached. This should not happen
 * if the journal was properly destroyed, but warn if it does to help debugging.
 * Clear the JBD flag and b_private to prevent issues with subsequent mounts.
 */
static void bh_clear_stale_jbd(struct buffer_head *bh)
{
	if (buffer_jbd(bh)) {
		log_err("bh %p block %llu still has JBD (b_private %p)\n",
			bh, (unsigned long long)bh->b_blocknr, bh->b_private);
		/*
		 * Clear the JBD flag and b_private to prevent issues.
		 * The journal_head itself will be freed when the
		 * journal_head cache is destroyed.
		 */
		clear_buffer_jbd(bh);
		bh->b_private = NULL;
	}
}

void bh_cache_clear(void)
{
	int i;
	struct bh_cache_entry *entry, *next;

	for (i = 0; i < BH_CACHE_SIZE; i++) {
		for (entry = bh_cache[i]; entry; entry = next) {
			next = entry->next;
			if (entry->bh) {
				struct buffer_head *bh = entry->bh;

				bh_clear_stale_jbd(bh);
				/*
				 * Force count to 1 so the buffer will be freed.
				 * On unmount, ext4 code won't access these
				 * buffers again, so extra references are stale.
				 */
				atomic_set(&bh->b_count, 1);
				if (atomic_dec_and_test(&bh->b_count))
					free_buffer_head(bh);
			}
			free(entry);
		}
		bh_cache[i] = NULL;
	}
}

/**
 * bh_cache_release_jbd() - Release all JBD references from buffer cache
 *
 * This must be called after journal destroy but before bh_cache_clear().
 * It ensures all journal_heads are properly released from buffer_heads
 * even if the journal destroy didn't fully clean up (e.g., on abort).
 */
void bh_cache_release_jbd(void)
{
	int i;
	struct bh_cache_entry *entry;

	for (i = 0; i < BH_CACHE_SIZE; i++) {
		for (entry = bh_cache[i]; entry; entry = entry->next) {
			if (entry->bh && buffer_jbd(entry->bh)) {
				struct buffer_head *bh = entry->bh;
				struct journal_head *jh = bh2jh(bh);

				/*
				 * Forcibly release the journal_head.
				 * Clear b_bh to prevent use-after-free when
				 * the buffer_head is later freed.
				 */
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
 * U-Boot doesn't have a journal thread, so we need to manually sync
 * all dirty buffers after write operations.
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
				int err = ext4l_write_block(entry->bh->b_blocknr,
							    entry->bh->b_size,
							    entry->bh->b_data);
				if (err && !ret)
					ret = err;
				clear_buffer_dirty(entry->bh);
			}
		}
	}
	return ret;
}

/**
 * alloc_buffer_head() - Allocate a buffer_head structure
 * @gfp_mask: Allocation flags (ignored in U-Boot)
 * Return: Pointer to buffer_head or NULL on error
 */
struct buffer_head *alloc_buffer_head(gfp_t gfp_mask)
{
	struct buffer_head *bh;

	bh = malloc(sizeof(struct buffer_head));
	if (!bh)
		return NULL;

	memset(bh, 0, sizeof(struct buffer_head));

	/* Note: b_data will be allocated when needed by read functions */
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

	bh = malloc(sizeof(struct buffer_head));
	if (!bh)
		return NULL;

	memset(bh, 0, sizeof(struct buffer_head));

	bh->b_data = malloc(size);
	if (!bh->b_data) {
		free(bh);
		return NULL;
	}

	bh->b_size = size;
	/* Allocate a folio for kmap_local_folio() to work */
	bh->b_folio = malloc(sizeof(struct folio));
	if (bh->b_folio) {
		memset(bh->b_folio, 0, sizeof(struct folio));
		bh->b_folio->data = bh->b_data;
	}
	atomic_set(&bh->b_count, 1);
	/* Mark that this buffer owns its b_data and should free it */
	set_bit(BH_OwnsData, &bh->b_state);

	return bh;
}

/**
 * free_buffer_head() - Free a buffer_head
 * @bh: Buffer head to free
 *
 * Only free b_data if BH_OwnsData is set. Shadow buffers created by
 * jbd2_journal_write_metadata_buffer() share b_data/b_folio with the original
 * buffer and should not free them. Shadow buffers are identified by having
 * b_private set to point to the original buffer.
 */
void free_buffer_head(struct buffer_head *bh)
{
	if (!bh)
		return;

	/*
	 * Never free a buffer_head that has a journal_head attached.
	 * This would cause use-after-free when the journal tries to access it.
	 * The journal owns a reference and the buffer will be cleaned up when
	 * the journal_head is properly released.
	 */
	if (buffer_jbd(bh))
		return;

	/*
	 * Shadow buffers (b_private != NULL) share their folio with the
	 * original buffer. Don't free the shared folio.
	 */
	if (!bh->b_private && bh->b_folio)
		free(bh->b_folio);

	/* Only free b_data if this buffer owns it */
	if (bh->b_data && test_bit(BH_OwnsData, &bh->b_state))
		free(bh->b_data);
	free(bh);
}

/**
 * ext4l_read_block() - Read a block from the block device
 * @block: Block number (filesystem block, not sector)
 * @size: Block size in bytes
 * @buffer: Destination buffer
 * Return: 0 on success, negative on error
 */
int ext4l_read_block(sector_t block, size_t size, void *buffer)
{
	struct disk_partition *part;
	lbaint_t sector, count;
	struct blk_desc *desc;
	struct udevice *blk;
	ulong n;

	blk = ext4l_get_blk();
	part = ext4l_get_partition();
	if (!blk)
		return -EIO;

	desc = dev_get_uclass_plat(blk);

	/* Convert block to sector */
	sector = (block * size) / desc->blksz + part->start;
	count = size / desc->blksz;

	if (count == 0)
		count = 1;

	n = blk_read(blk, sector, count, buffer);
	if (n != count)
		return -EIO;

	return 0;
}

/**
 * ext4l_write_block() - Write a block to the block device
 * @block: Block number (filesystem block, not sector)
 * @size: Block size in bytes
 * @buffer: Source buffer
 * Return: 0 on success, negative on error
 */
int ext4l_write_block(sector_t block, size_t size, void *buffer)
{
	struct disk_partition *part;
	lbaint_t sector, count;
	struct blk_desc *desc;
	struct udevice *blk;
	ulong n;

	blk = ext4l_get_blk();
	part = ext4l_get_partition();
	if (!blk)
		return -EIO;

	desc = dev_get_uclass_plat(blk);

	/* Convert block to sector */
	sector = (block * size) / desc->blksz + part->start;
	count = size / desc->blksz;

	if (count == 0)
		count = 1;

	n = blk_write(blk, sector, count, buffer);
	if (n != count)
		return -EIO;

	return 0;
}

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

	/* Check cache first - must match block number AND size */
	bh = bh_cache_lookup(block, sb->s_blocksize);
	if (bh)
		return bh;

	/* Allocate new buffer */
	bh = alloc_buffer_head_with_data(sb->s_blocksize);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = sb->s_bdev;
	bh->b_size = sb->s_blocksize;

	/* Mark buffer as having a valid disk mapping */
	set_buffer_mapped(bh);

	/* Don't read - just allocate with zeroed data */
	memset(bh->b_data, '\0', bh->b_size);

	/* Add to cache */
	bh_cache_insert(bh);

	return bh;
}

/**
 * __getblk() - Get a buffer for a given block device
 * @bdev: Block device
 * @block: Block number
 * @size: Block size
 * Return: Buffer head or NULL on error
 *
 * Similar to sb_getblk but takes a block device instead of superblock.
 * Used by the journal to allocate descriptor buffers.
 */
struct buffer_head *__getblk(struct block_device *bdev, sector_t block,
			     unsigned int size)
{
	struct buffer_head *bh;

	if (!bdev || !size)
		return NULL;

	/* Check cache first - must match block number AND size */
	bh = bh_cache_lookup(block, size);
	if (bh)
		return bh;

	/* Allocate new buffer */
	bh = alloc_buffer_head_with_data(size);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = bdev;
	bh->b_size = size;

	/* Mark buffer as having a valid disk mapping */
	set_buffer_mapped(bh);

	/* Don't read - just allocate with zeroed data */
	memset(bh->b_data, '\0', bh->b_size);

	/* Add to cache */
	bh_cache_insert(bh);

	return bh;
}

/**
 * sb_bread() - Read a block via super_block
 * @sb: Super block
 * @block: Block number to read
 * Return: Buffer head or NULL on error
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

	/* If buffer is already up-to-date, return it without re-reading */
	if (buffer_uptodate(bh))
		return bh;

	bh->b_blocknr = block;
	bh->b_bdev = sb->s_bdev;
	bh->b_size = sb->s_blocksize;

	ret = ext4l_read_block(block, sb->s_blocksize, bh->b_data);
	if (ret) {
		brelse(bh);
		return NULL;
	}

	/* Mark buffer as up-to-date */
	set_buffer_uptodate(bh);

	return bh;
}

/**
 * brelse() - Release a buffer_head
 * @bh: Buffer head to release
 *
 * Decrements the reference count on the buffer. Cached buffer heads are
 * freed by bh_cache_clear() on unmount, so this just decrements the count.
 * Non-cached buffers are freed when the count reaches zero.
 */
void brelse(struct buffer_head *bh)
{
	if (!bh)
		return;

	/*
	 * If buffer has JBD attached, don't let ref count go to zero.
	 * The journal owns a reference and will clean up properly.
	 */
	if (buffer_jbd(bh) && atomic_read(&bh->b_count) <= 1)
		return;

	if (atomic_dec_and_test(&bh->b_count) && !buffer_cached(bh))
		free_buffer_head(bh);
}

/**
 * __brelse() - Release a buffer_head reference without freeing
 * @bh: Buffer head to release
 *
 * Unlike brelse(), this only decrements the reference count without
 * freeing the buffer when count reaches zero. Used when caller will
 * explicitly free with free_buffer_head() afterward.
 */
void __brelse(struct buffer_head *bh)
{
	if (bh)
		atomic_dec(&bh->b_count);
}

/**
 * bdev_getblk() - Get buffer via block_device
 * @bdev: Block device
 * @block: Block number
 * @size: Block size
 * @gfp: Allocation flags
 * Return: Buffer head or NULL
 */
struct buffer_head *bdev_getblk(struct block_device *bdev, sector_t block,
				unsigned size, gfp_t gfp)
{
	struct buffer_head *bh;

	/* Check cache first - must match block number AND size */
	bh = bh_cache_lookup(block, size);
	if (bh)
		return bh;

	bh = alloc_buffer_head_with_data(size);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = bdev;
	bh->b_size = size;

	/* Mark buffer as having a valid disk mapping */
	set_buffer_mapped(bh);

	/* Don't read - just allocate with zeroed data */
	memset(bh->b_data, 0, bh->b_size);

	/* Add to cache */
	bh_cache_insert(bh);

	return bh;
}

/**
 * __bread() - Read a block via block_device
 * @bdev: Block device
 * @block: Block number to read
 * @size: Block size
 * Return: Buffer head or NULL on error
 */
struct buffer_head *__bread(struct block_device *bdev, sector_t block,
			    unsigned size)
{
	struct buffer_head *bh;
	int ret;

	bh = alloc_buffer_head_with_data(size);
	if (!bh)
		return NULL;

	bh->b_blocknr = block;
	bh->b_bdev = bdev;
	bh->b_size = size;

	ret = ext4l_read_block(block, size, bh->b_data);
	if (ret) {
		free_buffer_head(bh);
		return NULL;
	}

	/* Mark buffer as up-to-date */
	set_bit(BH_Uptodate, &bh->b_state);

	return bh;
}

/**
 * end_buffer_write_sync() - Completion handler for synchronous buffer writes
 * @bh: Buffer head that completed I/O
 * @uptodate: 1 if I/O succeeded, 0 if failed
 *
 * This callback is invoked after a buffer write completes. It sets the
 * buffer's uptodate state based on the result and unlocks the buffer.
 */
void end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/**
 * submit_bh() - Submit a buffer_head for I/O
 * @op: Operation (REQ_OP_READ, REQ_OP_WRITE, etc.)
 * @bh: Buffer head to submit
 * Return: 0 on success, negative on error
 */
int submit_bh(int op, struct buffer_head *bh)
{
	int ret = 0;
	int op_type = op & REQ_OP_MASK;  /* Mask out flags, keep operation type */
	int uptodate;

	if (op_type == REQ_OP_READ) {
		ret = ext4l_read_block(bh->b_blocknr, bh->b_size, bh->b_data);
		if (ret) {
			clear_buffer_uptodate(bh);
			uptodate = 0;
		} else {
			set_buffer_uptodate(bh);
			uptodate = 1;
		}
	} else if (op_type == REQ_OP_WRITE) {
		ret = ext4l_write_block(bh->b_blocknr, bh->b_size, bh->b_data);
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

	/* Call b_end_io callback if set - U-Boot does sync I/O */
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
