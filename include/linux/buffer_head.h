/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/buffer_head.h
 *
 * Everything to do with buffer_heads.
 * Minimal version for U-Boot ext4l - based on Linux 6.18
 */

#ifndef _LINUX_BUFFER_HEAD_H
#define _LINUX_BUFFER_HEAD_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <asm-generic/atomic.h>

enum bh_state_bits {
	BH_Uptodate,	/* Contains valid data */
	BH_Dirty,	/* Is dirty */
	BH_Lock,	/* Is locked */
	BH_Req,		/* Has been submitted for I/O */

	BH_Mapped,	/* Has a disk mapping */
	BH_New,		/* Disk mapping was newly created by get_block */
	BH_Async_Read,	/* Is under end_buffer_async_read I/O */
	BH_Async_Write,	/* Is under end_buffer_async_write I/O */
	BH_Delay,	/* Buffer is not yet allocated on disk */
	BH_Boundary,	/* Block is followed by a discontiguity */
	BH_Write_EIO,	/* I/O error on write */
	BH_Unwritten,	/* Buffer is allocated on disk but not written */
	BH_Quiet,	/* Buffer Error Prinks to be quiet */
	BH_Meta,	/* Buffer contains metadata */
	BH_Prio,	/* Buffer should be submitted with REQ_PRIO */
	BH_Defer_Completion, /* Defer AIO completion to workqueue */
	BH_Migrate,     /* Buffer is being migrated (norefs) */

	BH_PrivateStart,/* not a state bit, but the first bit available
			 * for private allocation by other entities
			 */
};

#define MAX_BUF_PER_PAGE (PAGE_SIZE / 512)

struct page;
struct folio;
struct buffer_head;
struct address_space;
struct block_device;
typedef void (bh_end_io_t)(struct buffer_head *bh, int uptodate);

/*
 * Historically, a buffer_head was used to map a single block
 * within a page, and of course as the unit of I/O through the
 * filesystem and block layers.  Nowadays the basic I/O unit
 * is the bio, and buffer_heads are used for extracting block
 * mappings (via a get_block_t call), for tracking state within
 * a folio (via a folio_mapping) and for wrapping bio submission
 * for backward compatibility reasons (e.g. submit_bh).
 */
struct buffer_head {
	unsigned long b_state;		/* buffer state bitmap (see above) */
	struct buffer_head *b_this_page;/* circular list of page's buffers */
	union {
		struct page *b_page;	/* the page this bh is mapped to */
		struct folio *b_folio;	/* the folio this bh is mapped to */
	};

	sector_t b_blocknr;		/* start block number */
	size_t b_size;			/* size of mapping */
	char *b_data;			/* pointer to data within the page */

	struct block_device *b_bdev;
	bh_end_io_t *b_end_io;		/* I/O completion */
	void *b_private;		/* reserved for b_end_io */
	struct list_head b_assoc_buffers; /* associated with another mapping */
	struct address_space *b_assoc_map;	/* mapping this buffer is
						   associated with */
	atomic_t b_count;		/* users using this buffer_head */
	spinlock_t b_uptodate_lock;	/* Used by the first bh in a page, to
					 * serialise IO completion of other
					 * buffers in the page */
};

/*
 * Buffer head bit operations - inline implementations.
 * These don't use the extern set_bit/clear_bit from asm/bitops.h
 * since sandbox doesn't implement them.
 */
static inline void bh_set_bit(int nr, unsigned long *addr)
{
	*addr |= (1UL << nr);
}

static inline void bh_clear_bit(int nr, unsigned long *addr)
{
	*addr &= ~(1UL << nr);
}

static inline int bh_test_bit(int nr, const unsigned long *addr)
{
	return (*addr >> nr) & 1;
}

static inline int bh_test_and_set_bit(int nr, unsigned long *addr)
{
	int old = (*addr >> nr) & 1;
	*addr |= (1UL << nr);
	return old;
}

static inline int bh_test_and_clear_bit(int nr, unsigned long *addr)
{
	int old = (*addr >> nr) & 1;
	*addr &= ~(1UL << nr);
	return old;
}

/*
 * macro tricks to expand the set_buffer_foo(), clear_buffer_foo()
 * and buffer_foo() functions.
 */
#define BUFFER_FNS(bit, name)						\
static __always_inline void set_buffer_##name(struct buffer_head *bh)	\
{									\
	if (!bh_test_bit(BH_##bit, &(bh)->b_state))			\
		bh_set_bit(BH_##bit, &(bh)->b_state);			\
}									\
static __always_inline void clear_buffer_##name(struct buffer_head *bh)	\
{									\
	bh_clear_bit(BH_##bit, &(bh)->b_state);				\
}									\
static __always_inline int buffer_##name(const struct buffer_head *bh)	\
{									\
	return bh_test_bit(BH_##bit, &(bh)->b_state);			\
}

/*
 * test_set_buffer_foo() and test_clear_buffer_foo()
 */
#define TAS_BUFFER_FNS(bit, name)					\
static __always_inline int test_set_buffer_##name(struct buffer_head *bh) \
{									\
	return bh_test_and_set_bit(BH_##bit, &(bh)->b_state);		\
}									\
static __always_inline int test_clear_buffer_##name(struct buffer_head *bh) \
{									\
	return bh_test_and_clear_bit(BH_##bit, &(bh)->b_state);		\
}

BUFFER_FNS(Uptodate, uptodate)
BUFFER_FNS(Dirty, dirty)
BUFFER_FNS(Lock, locked)
BUFFER_FNS(Req, req)
BUFFER_FNS(Mapped, mapped)
BUFFER_FNS(New, new)
BUFFER_FNS(Async_Read, async_read)
BUFFER_FNS(Async_Write, async_write)
BUFFER_FNS(Delay, delay)
BUFFER_FNS(Boundary, boundary)
BUFFER_FNS(Write_EIO, write_io_error)
BUFFER_FNS(Unwritten, unwritten)
BUFFER_FNS(Meta, meta)
BUFFER_FNS(Prio, prio)
BUFFER_FNS(Defer_Completion, defer_completion)

static inline void get_bh(struct buffer_head *bh)
{
	atomic_inc(&bh->b_count);
}

static inline void put_bh(struct buffer_head *bh)
{
	atomic_dec(&bh->b_count);
}

/* Buffer release functions - implemented in ext4l/interface.c */
void brelse(struct buffer_head *bh);
void __brelse(struct buffer_head *bh);

/*
 * Buffer operation stubs - U-Boot is single-threaded
 */
#define wait_on_buffer(bh)		do { } while (0)
#define __bforget(bh)			do { } while (0)
#define lock_buffer(bh)			set_buffer_locked(bh)
#define unlock_buffer(bh)		clear_buffer_locked(bh)
#define test_clear_buffer_dirty(bh)	({ (void)(bh); 0; })

/* Buffer I/O submission - implemented in ext4l/stub.c */
int submit_bh(int op_flags, struct buffer_head *bh);

/* Buffer read functions - implemented in ext4l/support.c */
int bh_read(struct buffer_head *bh, int flags);
#define bh_read_nowait(bh, flags)	bh_read(bh, flags)
#define bh_readahead_batch(n, bhs, f)	do { (void)(n); (void)(bhs); (void)(f); } while (0)

/*
 * Buffer dirty operations.
 * In U-Boot we write buffers synchronously, so marking dirty writes immediately.
 */
#define sync_dirty_buffer(bh)		submit_bh(REQ_OP_WRITE, (bh))
#define mark_buffer_dirty(bh)		sync_dirty_buffer(bh)
#define mark_buffer_dirty_inode(bh, i)	sync_dirty_buffer(bh)
#define write_dirty_buffer(bh, flags)	sync_dirty_buffer(bh)

/* Buffer uptodate check - always returns true (buffer assumed uptodate) */
#define bh_uptodate_or_lock(bh)		(1)

/* Buffer allocation functions - implemented in ext4l */
struct super_block;
struct buffer_head *alloc_buffer_head(gfp_t gfp_mask);
void free_buffer_head(struct buffer_head *bh);
struct buffer_head *sb_getblk(struct super_block *sb, sector_t block);
struct buffer_head *__getblk(struct block_device *bdev, sector_t block,
			     unsigned int size);
#define sb_getblk_gfp(sb, blk, gfp)	sb_getblk((sb), (blk))
#define getblk_unmovable(bdev, block, size) \
	sb_getblk((bdev)->bd_super, (block))

/*
 * Folio migration stubs - U-Boot doesn't support memory migration
 */
static inline int buffer_migrate_folio(struct address_space *mapping,
				       struct folio *dst, struct folio *src,
				       int mode)
{
	return -EOPNOTSUPP;
}

static inline int buffer_migrate_folio_norefs(struct address_space *mapping,
					      struct folio *dst,
					      struct folio *src, int mode)
{
	return -EOPNOTSUPP;
}

/*
 * noop_dirty_folio - no-op dirty folio handler
 */
static inline bool noop_dirty_folio(struct address_space *mapping,
				    struct folio *folio)
{
	return false;
}

/*
 * end_buffer_read_sync - completion handler for synchronous buffer reads
 * @bh: buffer head that completed
 * @uptodate: whether the read was successful
 */
static inline void end_buffer_read_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/*
 * Buffer cache lookup stubs - U-Boot doesn't maintain a buffer cache
 */
#define sb_find_get_block(sb, block) \
	({ (void)(sb); (void)(block); (struct buffer_head *)NULL; })
#define sb_find_get_block_nonatomic(sb, block) \
	({ (void)(sb); (void)(block); (struct buffer_head *)NULL; })
#define __find_get_block_nonatomic(bdev, block, size) \
	({ (void)(bdev); (void)(block); (void)(size); (struct buffer_head *)NULL; })

/*
 * Block/buffer folio operations - U-Boot stubs
 */
#define create_empty_buffers(f, s, flags) \
	({ (void)(f); (void)(s); (void)(flags); (struct buffer_head *)NULL; })
/* bh_offset returns offset of b_data within the folio */
#define bh_offset(bh)			((bh)->b_folio ? \
	(unsigned long)((char *)(bh)->b_data - (char *)(bh)->b_folio->data) : 0UL)
#define block_invalidate_folio(f, o, l)	do { } while (0)
#define block_write_end(pos, len, copied, folio) \
	({ (void)(pos); (void)(len); (void)(folio); (copied); })
#define block_dirty_folio(m, f)		({ (void)(m); (void)(f); false; })
#define try_to_free_buffers(f)		({ (void)(f); true; })
#define block_commit_write(f, f2, t)	do { } while (0)
#define block_page_mkwrite(v, f, g)	((vm_fault_t)0)
#define map_bh(bh, sb, blk) \
	do { (bh)->b_blocknr = (blk); set_buffer_mapped(bh); } while (0)
#define block_read_full_folio(folio, get_block) \
	({ (void)(folio); (void)(get_block); 0; })

#endif /* _LINUX_BUFFER_HEAD_H */
