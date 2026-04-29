/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * fat_internal.h
 *
 * Internal definitions and structures for FAT filesystem implementation
 */

#ifndef _FAT_INTERNAL_H_
#define _FAT_INTERNAL_H_

#include <fat.h>
#include <linux/compiler.h>

struct blk_desc;
struct disk_partition;

/* Maximum number of clusters for FAT12 */
#define MAX_FAT12	0xFF4

/* Boot sector offsets */
#define DOS_BOOT_MAGIC_OFFSET	0x1fe
#define DOS_FS_TYPE_OFFSET	0x36
#define DOS_FS32_TYPE_OFFSET	0x52

#define TYPE_FILE 0x1
#define TYPE_DIR  0x2
#define TYPE_ANY  (TYPE_FILE | TYPE_DIR)

/* Global variables shared between fat.c and fat_write.c */
extern struct blk_desc *cur_dev;
extern struct disk_partition cur_part_info;

/**
 * struct fat_itr - directory iterator, to simplify filesystem traversal
 * @fsdata: filesystem parameters
 * @start_clust: first cluster
 * @clust: current cluster
 * @next_clust: next cluster if remaining == 0
 * @last_cluster: set if last cluster of directory reached
 * @is_root: is iterator at root directory
 * @remaining: remaining directory entries in current cluster
 * @dent: current directory entry
 * @dent_rem: remaining entries after long name start
 * @dent_clust: cluster of long name start
 * @dent_start: first directory entry for long name
 * @l_name: long name of current directory entry
 * @s_name: short 8.3 name of current directory entry
 * @name: l_name if there is one, else s_name
 * @block: buffer for current cluster
 *
 * Implements an iterator pattern to traverse directory tables,
 * transparently handling directory tables split across multiple
 * clusters, and the difference between FAT12/FAT16 root directory
 * (contiguous) and subdirectories + FAT32 root (chained).
 *
 * Rough usage
 *
 * .. code-block:: c
 *
 *     for (fat_itr_root(&itr, fsdata); fat_itr_next(&itr); ) {
 *         // to traverse down to a subdirectory pointed to by
 *         // current iterator position:
 *         fat_itr_child(&itr, &itr);
 *     }
 *
 * For a more complete example, see fat_itr_resolve().
 */
struct fat_itr {
	struct fsdata *fsdata;
	unsigned int start_clust;
	unsigned int clust;
	unsigned int next_clust;
	int last_cluster;
	int is_root;
	int remaining;
	struct dir_entry *dent;
	int dent_rem;
	unsigned int dent_clust;
	struct dir_entry *dent_start;
	char l_name[VFAT_MAXLEN_BYTES];
	char s_name[14];
	char *name;
	u8 block[MAX_CLUSTSIZE] __aligned(ARCH_DMA_MINALIGN);
};

/**
 * downcase() - convert a string to lowercase
 * @str: string to convert
 * @len: maximum number of characters to convert
 */
void downcase(char *str, size_t len);

/**
 * next_dent() - get next directory entry
 * @itr: directory iterator
 * Return: pointer to next directory entry, or NULL if at end
 */
struct dir_entry *next_dent(struct fat_itr *itr);

/**
 * disk_read() - read sectors from the current FAT device
 * @block: logical block number
 * @nr_blocks: number of blocks to read
 * @buf: buffer to read data into
 * Return: number of blocks read, -1 on error
 */
int disk_read(u32 block, u32 nr_blocks, void *buf);

#if IS_ENABLED(CONFIG_FS_FAT_HANDLE_SECTOR_SIZE_MISMATCH)
/**
 * disk_write() - write sectors to the current FAT device
 * @block: logical block number
 * @nr_blocks: number of blocks to write
 * @buf: buffer holding data to write
 * Return: number of blocks written, -1 on error
 *
 * Defined in fat.c when CONFIG_FS_FAT_HANDLE_SECTOR_SIZE_MISMATCH is
 * enabled; otherwise fat_write.c provides a static version.
 */
int disk_write(u32 block, u32 nr_blocks, void *buf);
#endif

/**
 * disk_write() - write sectors to the current FAT device
 * @block: logical block number
 * @nr_blocks: number of blocks to write
 * @buf: buffer holding data to write
 * Return: number of blocks written, -1 on error
 */
int disk_write(u32 block, u32 nr_blocks, void *buf);

/**
 * flush_dirty_fat_buffer() - write fat buffer to disk if dirty
 * @mydata: filesystem data
 * Return: 0 on success, -1 on error
 */
int flush_dirty_fat_buffer(struct fsdata *mydata);

/* Internal function declarations */

/**
 * get_fatent() - get the entry at index 'entry' in a FAT (12/16/32) table
 * @mydata: filesystem data
 * @entry: FAT entry index
 * Return: FAT entry value, 0x00 on failure
 */
u32 get_fatent(struct fsdata *mydata, u32 entry);

/**
 * mkcksum() - calculate short name checksum
 * @nameext: name and extension structure
 * Return: checksum value
 */
u8 mkcksum(struct nameext *nameext);

/**
 * fat_itr_root() - initialize an iterator to start at the root directory
 * @itr: iterator to initialize
 * @fsdata: filesystem data for the partition
 * Return: 0 on success, else -errno
 */
int fat_itr_root(struct fat_itr *itr, struct fsdata *fsdata);

/**
 * fat_itr_child() - initialize an iterator to descend into a sub-directory
 * @itr: iterator to initialize
 * @parent: the iterator pointing at a directory entry in the parent directory
 */
void fat_itr_child(struct fat_itr *itr, struct fat_itr *parent);

/**
 * fat_itr_next() - step to the next entry in a directory
 * @itr: the iterator to iterate
 * Return: 1 if success or 0 if no more entries in the current directory
 */
int fat_itr_next(struct fat_itr *itr);

/**
 * fat_itr_isdir() - is current cursor position pointing to a directory
 * @itr: the iterator
 * Return: true if cursor is at a directory
 */
int fat_itr_isdir(struct fat_itr *itr);

/**
 * fat_itr_resolve() - traverse directory structure to resolve the requested path
 * @itr: iterator initialized to root
 * @path: the requested path
 * @type: bitmask of allowable file types
 * Return: 0 on success or -errno
 */
int fat_itr_resolve(struct fat_itr *itr, const char *path, uint type);

#endif /* _FAT_INTERNAL_H_ */
