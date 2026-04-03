/* SPDX-License-Identifier: GPL-2.0 */
/*
 * U-Boot compatibility header for ext4l filesystem
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * This provides minimal definitions to allow Linux ext4 code to compile
 * in U-Boot.
 */

#ifndef __EXT4_UBOOT_H__
#define __EXT4_UBOOT_H__

/*
 * Suppress warnings for unused static functions and variables in Linux ext4
 * source files. These are used in code paths that are stubbed out in U-Boot.
 */
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"

/* U-Boot headers */
#include <div64.h>
#include <hexdump.h>
#include <u-boot/crc.h>
#include <vsprintf.h>

/* Linux types - must come first */
#include <linux/types.h>

/* Linux headers (alphabetical) */
#include <asm/byteorder.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/build_bug.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/iomap.h>
#include <linux/list.h>
#include <linux/log2.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/pagevec.h>
#include <linux/rbtree.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/workqueue.h>

/*
 * BUG_ON / BUG - stubs (not using linux/bug.h which panics)
 * In Linux, these indicate kernel bugs. In ext4l, some BUG_ON conditions
 * that check for race conditions can trigger in single-threaded U-Boot,
 * so we stub them out as no-ops.
 */
#undef BUG_ON
#undef BUG
#define BUG_ON(cond)	do { (void)(cond); } while (0)
#define BUG()		do { } while (0)

/* Local ext4l headers */
#include "ext4_fscrypt.h"
#include "ext4_trace.h"

/*
 * Override no_printk to avoid format warnings in disabled debug prints.
 * The Linux kernel uses sector_t as u64, but U-Boot uses unsigned long.
 * This causes format mismatches with %llu that we want to ignore.
 */
#undef no_printk
#define no_printk(fmt, ...)	({ 0; })

/* More Linux headers (alphabetical) */
#include <asm-generic/atomic.h>
#include <asm-generic/bitops/le.h>
#include <asm-generic/bitops/lock.h>
#include <asm-generic/timex.h>
#include <kunit/static_stub.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/blockgroup_lock.h>
#include <linux/buffer_head.h>
#include <linux/cache.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/crc16.h>
#include <linux/crc32c.h>
#include <linux/ctype.h>
#include <linux/dax.h>
#include <linux/dcache.h>
#include <linux/delayed_call.h>
#include <linux/exportfs.h>
#include <linux/fiemap.h>
#include <linux/fsmap.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/fsverity.h>
#include <linux/hash.h>
#include <linux/highuid.h>
#include <linux/hrtimer.h>
#include <linux/ioprio.h>
#include <linux/iversion.h>
#include <linux/jbd2.h>
#include <linux/jiffies.h>
#include <linux/kdev_t.h>
#include <linux/kobject.h>
#include <linux/ktime.h>
#include <linux/list_sort.h>
#include <linux/lockdep.h>
#include <linux/magic.h>
#include <linux/mbcache.h>
#include <linux/mempool.h>
#include <linux/mm_types.h>
#include <linux/mnt_idmapping.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/nospec.h>
#include <linux/overflow.h>
#include <linux/pagemap.h>
#include <linux/path.h>
#include <linux/percpu.h>
#include <linux/percpu_counter.h>
#include <linux/prefetch.h>
#include <linux/proc_fs.h>
#include <linux/projid.h>
#include <linux/quotaops.h>
#include <linux/random.h>
#include <linux/ratelimit.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/sort.h>
#include <linux/spinlock.h>
#include <linux/statfs.h>
#include <linux/uio.h>
#include <linux/utsname.h>
#include <linux/uuid.h>
#include <linux/wait_bit.h>
#include <linux/writeback.h>
#include <linux/xarray.h>
#include <linux/xattr.h>

/*
 * Hex dump - Linux kernel print_hex_dump has a different signature
 * (includes log level) than U-Boot's, so we stub it out here.
 */
#undef print_hex_dump
#define print_hex_dump(l, p, pt, rg, gc, b, len, a) do { } while (0)

/*
 * __CHAR_UNSIGNED__ - directory hash algorithm selection
 *
 * The ext4 filesystem uses different hash algorithms for directory indexing
 * depending on whether the platform's 'char' type is signed or unsigned.
 * GCC automatically defines __CHAR_UNSIGNED__ on platforms where char is
 * unsigned (e.g., ARM), and leaves it undefined where char is signed
 * (e.g., x86/sandbox).
 *
 * The filesystem stores EXT2_FLAGS_UNSIGNED_HASH or EXT2_FLAGS_SIGNED_HASH
 * in the superblock to record which hash variant was used when the filesystem
 * was created, ensuring correct behavior regardless of the mounting platform.
 *
 * See super.c:5123 and ioctl.c:1489 for the hash algorithm selection code.
 */

/* ext4-specific constants */
#define EXT4_FIEMAP_EXTENT_HOLE		0x08000000

#define EXT4_GOING_FLAGS_DEFAULT	0
#define EXT4_GOING_FLAGS_LOGFLUSH	1
#define EXT4_GOING_FLAGS_NOLOGFLUSH	2

/*
 * U-Boot buffer head private bits.
 *
 * Start at BH_JBDPrivateStart + 1 because ext4.h uses BH_JBDPrivateStart
 * for BH_BITMAP_UPTODATE.
 */
#define BH_OwnsData		(BH_JBDPrivateStart + 1)
BUFFER_FNS(OwnsData, ownsdata)

/*
 * U-Boot: marks buffer is in the buffer cache.
 * Cached buffers are freed by bh_cache_clear(), not brelse().
 */
#define BH_Cached		(BH_JBDPrivateStart + 2)
BUFFER_FNS(Cached, cached)

/* Forward declarations */
struct fid;
struct kstat;
struct kstatfs;
struct path;
struct pipe_inode_info;

/* FSTR_INIT - fscrypt_str initializer (fscrypt_str defined in ext4_fscrypt.h) */
#define FSTR_INIT(n, l)		{ .name = (n), .len = (l) }

/* Part stat - not used in U-Boot. Note: sectors[X] is passed as second arg */
#define STAT_READ		0
#define STAT_WRITE		0
static u64 __attribute__((unused)) __ext4_sectors[2];
#define sectors			__ext4_sectors
#define part_stat_read(p, f)	({ (void)(p); (void)(f); 0ULL; })

/* CRC32 big-endian - map to U-Boot's crc32() */
#define crc32_be(crc, p, len)		crc32(crc, p, len)

/* Memory allocation for journal.c */
#define __get_free_pages(gfp, order)	((unsigned long)memalign(PAGE_SIZE, PAGE_SIZE << (order)))
#define free_pages(addr, order)		free((void *)(addr))
#define get_order(size)			ilog2(roundup_pow_of_two((size) / PAGE_SIZE))

/* Memory allocation - declarations for stub.c */
void *kvzalloc(size_t size, gfp_t flags);
#define kvmalloc(size, flags)	kvzalloc(size, flags)

/* Page allocation - declarations for stub.c */
unsigned long get_zeroed_page(gfp_t gfp);
void free_page(unsigned long addr);

/* DAX - declarations for stub.c */
void *fs_dax_get_by_bdev(struct block_device *bdev, u64 *start, u64 *len,
			 void *holder);
void fs_put_dax(void *dax, void *holder);

/* Buffer submission - declaration for stub.c */
int trylock_buffer(struct buffer_head *bh);

/* Filesystem sync - declaration for stub.c */
int sync_filesystem(void *sb);

/* Trace stubs for super.c - declaration for stub.c */
void trace_ext4_error(struct super_block *sb, const char *func, unsigned int line);

/* end_buffer_write_sync - implemented in support.c */
void end_buffer_write_sync(struct buffer_head *bh, int uptodate);

/* ext4 superblock initialisation and commit */
int ext4_commit_super(struct super_block *sb);
int ext4_fill_super(struct super_block *sb, struct fs_context *fc);
void ext4_unregister_li_request(struct super_block *sb);

/* ext4l support functions (support.c) */
int bh_cache_sync(void);
int ext4l_read_block(struct block_device *bdev, sector_t block, size_t size,
		     void *buffer);
int ext4l_write_block(struct block_device *bdev, sector_t block, size_t size,
		      void *buffer);
struct membuf *ext4l_get_msg_buf(void);
void bh_cache_clear(struct block_device *bdev);
void bh_cache_release_jbd(struct block_device *bdev);
void ext4l_crc32c_init(void);
void ext4l_msg_init(void);
void ext4l_print_msgs(void);
void ext4l_record_msg(const char *msg, int len);


#endif /* __EXT4_UBOOT_H__ */
