/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Filesystem definitions
 *
 * Minimal version for U-Boot ext4l - based on Linux 6.18
 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/fs/super_types.h>
#include <linux/cred.h>
#include <linux/rwsem.h>
#include <linux/time.h>
#include <asm/atomic.h>

/* Forward declarations */
struct buffer_head;
struct file;
struct folio;
struct readahead_control;
struct kiocb;
struct writeback_control;
struct swap_info_struct;

/* errseq_t - error sequence type */
typedef u32 errseq_t;

/* fmode_t - file mode type */
typedef unsigned int fmode_t;

/* File mode flags */
#define FMODE_READ		((__force fmode_t)(1 << 0))
#define FMODE_WRITE		((__force fmode_t)(1 << 1))
#define FMODE_LSEEK		((__force fmode_t)(1 << 2))
#define FMODE_NOWAIT		((__force fmode_t)(1 << 20))
#define FMODE_CAN_ODIRECT	((__force fmode_t)(1 << 21))
#define FMODE_CAN_ATOMIC_WRITE	((__force fmode_t)(1 << 22))

/* Directory file mode flags - use low bits for hash mode */
#define FMODE_32BITHASH		((__force fmode_t)0x00000001)
#define FMODE_64BITHASH		((__force fmode_t)0x00000002)

/* Seek constants */
#ifndef SEEK_HOLE
#define SEEK_HOLE	4
#define SEEK_DATA	3
#endif

/* vfsmount - mount point */
struct vfsmount {
	struct dentry *mnt_root;
};

/* path - use linux/path.h */
#include <linux/path.h>

/* Buffer operations are in buffer_head.h */

#ifdef __UBOOT__
/* Maximum number of cached folios per address_space */
#define FOLIO_CACHE_MAX 64
#endif

/* address_space_operations - forward declare for address_space */
struct address_space_operations;

/* address_space - extended for inode.c */
struct address_space {
	struct inode *host;
	errseq_t wb_err;
	unsigned long nrpages;
	unsigned long writeback_index;
	struct list_head i_private_list;
	const struct address_space_operations *a_ops;
#ifdef __UBOOT__
	/* Simple folio cache for U-Boot (no XA/radix tree) */
	struct folio *folio_cache[FOLIO_CACHE_MAX];
	int folio_cache_count;
#endif
};

/* address_space_operations - filesystem address space methods */
struct address_space_operations {
	int (*read_folio)(struct file *, struct folio *);
	void (*readahead)(struct readahead_control *);
	sector_t (*bmap)(struct address_space *, sector_t);
	void (*invalidate_folio)(struct folio *, size_t, size_t);
	bool (*release_folio)(struct folio *, gfp_t);
	int (*write_begin)(const struct kiocb *, struct address_space *,
			   loff_t, unsigned, struct folio **, void **);
	int (*write_end)(const struct kiocb *, struct address_space *,
			 loff_t, unsigned, unsigned, struct folio *, void *);
	int (*writepages)(struct address_space *, struct writeback_control *);
	bool (*dirty_folio)(struct address_space *, struct folio *);
	bool (*is_partially_uptodate)(struct folio *, size_t, size_t);
	int (*error_remove_folio)(struct address_space *, struct folio *);
	int (*migrate_folio)(struct address_space *, struct folio *,
			     struct folio *, int);
	int (*swap_activate)(struct swap_info_struct *, struct file *,
			     sector_t *);
};

/* Forward declarations for inode */
struct inode_operations;
struct file_operations;

/**
 * struct inode - filesystem inode
 *
 * Core filesystem object representing a file, directory, or other entity.
 */
struct inode {
	struct super_block *i_sb;
	unsigned long i_ino;
	umode_t i_mode;
	unsigned int i_nlink;
	loff_t i_size;
	struct address_space *i_mapping;
	struct address_space i_data;
	kuid_t i_uid;
	kgid_t i_gid;
	unsigned long i_blocks;
	unsigned int i_generation;
	unsigned int i_flags;
	unsigned int i_blkbits;
	unsigned long i_state;
	struct timespec64 i_atime;
	struct timespec64 i_mtime;
	struct timespec64 i_ctime;
	struct list_head i_io_list;
	dev_t i_rdev;
	const struct inode_operations *i_op;
	const struct file_operations *i_fop;
	atomic_t i_writecount;		/* Count of writers */
	atomic_t i_count;		/* Reference count */
	struct rw_semaphore i_rwsem;	/* inode lock */
	const char *i_link;		/* Symlink target for fast symlinks */
	unsigned short i_write_hint;	/* Write life time hint */
#ifdef __UBOOT__
	struct list_head i_sb_list;	/* Linkage into super_block s_inodes */
#endif
};

/* Inode time accessors */
static inline struct timespec64 inode_get_atime(const struct inode *inode)
{
	return inode->i_atime;
}

static inline struct timespec64 inode_get_mtime(const struct inode *inode)
{
	return inode->i_mtime;
}

static inline struct timespec64 inode_get_ctime(const struct inode *inode)
{
	return inode->i_ctime;
}

static inline time_t inode_get_atime_sec(const struct inode *inode)
{
	return inode->i_atime.tv_sec;
}

static inline time_t inode_get_ctime_sec(const struct inode *inode)
{
	return inode->i_ctime.tv_sec;
}

static inline time_t inode_get_mtime_sec(const struct inode *inode)
{
	return inode->i_mtime.tv_sec;
}

static inline void inode_set_ctime(struct inode *inode, time_t sec, long nsec)
{
	inode->i_ctime.tv_sec = sec;
	inode->i_ctime.tv_nsec = nsec;
}

static inline void inode_set_atime(struct inode *inode, time_t sec, long nsec)
{
	inode->i_atime.tv_sec = sec;
	inode->i_atime.tv_nsec = nsec;
}

static inline void inode_set_mtime(struct inode *inode, time_t sec, long nsec)
{
	inode->i_mtime.tv_sec = sec;
	inode->i_mtime.tv_nsec = nsec;
}

static inline void simple_inode_init_ts(struct inode *inode)
{
	struct timespec64 ts = { .tv_sec = 0, .tv_nsec = 0 };

	inode->i_atime = ts;
	inode->i_mtime = ts;
	inode->i_ctime = ts;
}

static inline struct timespec64 inode_set_atime_to_ts(struct inode *inode,
						      struct timespec64 ts)
{
	inode->i_atime = ts;
	return ts;
}

static inline struct timespec64 inode_set_ctime_to_ts(struct inode *inode,
						      struct timespec64 ts)
{
	inode->i_ctime = ts;
	return ts;
}

/* Inode credential helpers */
static inline unsigned int i_uid_read(const struct inode *inode)
{
	return inode->i_uid.val;
}

static inline unsigned int i_gid_read(const struct inode *inode)
{
	return inode->i_gid.val;
}

/*
 * Inode state accessors - simplified for single-threaded U-Boot.
 * Linux uses READ_ONCE/WRITE_ONCE and lockdep assertions; we use direct access.
 */
static inline unsigned long inode_state_read_once(struct inode *inode)
{
	return inode->i_state;
}

static inline unsigned long inode_state_read(struct inode *inode)
{
	return inode->i_state;
}

static inline void inode_state_set_raw(struct inode *inode, unsigned long flags)
{
	inode->i_state |= flags;
}

static inline void inode_state_set(struct inode *inode, unsigned long flags)
{
	inode->i_state |= flags;
}

static inline void inode_state_clear_raw(struct inode *inode,
					 unsigned long flags)
{
	inode->i_state &= ~flags;
}

static inline void inode_state_clear(struct inode *inode, unsigned long flags)
{
	inode->i_state &= ~flags;
}

static inline void inode_state_assign_raw(struct inode *inode,
					  unsigned long flags)
{
	inode->i_state = flags;
}

static inline void inode_state_assign(struct inode *inode, unsigned long flags)
{
	inode->i_state = flags;
}

struct udevice;

/* block_device - minimal stub with U-Boot block I/O fields */
struct block_device {
	struct address_space *bd_mapping;
	void *bd_disk;
	struct super_block *bd_super;
	dev_t bd_dev;
	bool read_only;
	struct udevice *bd_blk;		/* U-Boot block device */
	unsigned long bd_part_start;	/* partition start (in device sectors) */
};

/* errseq functions - stubs */
static inline int errseq_check(errseq_t *eseq, errseq_t since)
{
	return 0;
}

static inline int errseq_check_and_advance(errseq_t *eseq, errseq_t *since)
{
	return 0;
}

/* File readahead state - stub */
struct file_ra_state {
	unsigned long start;
	unsigned int size;
	unsigned int async_size;
	unsigned int ra_pages;
	unsigned int mmap_miss;
	long long prev_pos;
};

/* file - minimal stub */
struct file {
	fmode_t f_mode;
	struct inode *f_inode;
	unsigned int f_flags;
	struct address_space *f_mapping;
	void *private_data;
	struct file_ra_state f_ra;
	struct path f_path;
	loff_t f_pos;
};

/* Get inode from file */
static inline struct inode *file_inode(struct file *f)
{
	return f->f_inode;
}

/* File modification tracking - stubs for U-Boot */
#define file_modified(file)		({ (void)(file); 0; })
#define file_accessed(file)		do { (void)(file); } while (0)
#define file_update_time(f)		do { } while (0)
#define file_write_and_wait_range(f, s, e)	({ (void)(f); (void)(s); (void)(e); 0; })
#define file_check_and_advance_wb_err(f)	({ (void)(f); 0; })

/* File path operations - implemented in ext4l/stub.c */
char *file_path(struct file *file, char *buf, int buflen);
struct block_device *file_bdev(struct file *file);

/* iattr - inode attributes for setattr */
struct iattr {
	unsigned int ia_valid;
	umode_t ia_mode;
	uid_t ia_uid;
	gid_t ia_gid;
	loff_t ia_size;
};

/* iattr valid flags - specify which fields of iattr are valid */
#define ATTR_MODE	(1 << 0)
#define ATTR_UID	(1 << 1)
#define ATTR_GID	(1 << 2)
#define ATTR_SIZE	(1 << 3)
#define ATTR_ATIME	(1 << 4)
#define ATTR_MTIME	(1 << 5)
#define ATTR_CTIME	(1 << 6)
#define ATTR_ATIME_SET	(1 << 7)
#define ATTR_MTIME_SET	(1 << 8)
#define ATTR_FORCE	(1 << 9)
#define ATTR_KILL_SUID	(1 << 11)
#define ATTR_KILL_SGID	(1 << 12)
#define ATTR_TIMES_SET	(ATTR_ATIME_SET | ATTR_MTIME_SET)

/* Attribute operations - stubs for U-Boot */
#define setattr_prepare(m, d, a)	({ (void)(m); (void)(d); (void)(a); 0; })
#define setattr_copy(m, i, a)		do { } while (0)
#define posix_acl_chmod(m, i, mo)	({ (void)(m); (void)(i); (void)(mo); 0; })

/* writeback_control - defined in linux/compat.h */

/* fsnotify - stubs */
#define fsnotify_change(d, m)	do { } while (0)

/* fsnotify_sb_error - implemented in ext4l/stub.c */
void fsnotify_sb_error(struct super_block *sb, struct inode *inode, int error);

/* inode_init_once - stub */
static inline void inode_init_once(struct inode *inode)
{
}

/* S_ISDIR, etc. - already in linux/stat.h */
#include <linux/stat.h>

/* Inode flags for i_flags field */
#define S_SYNC		1	/* Synchronous writes */
#define S_NOATIME	2	/* No access time updates */
#define S_APPEND	4	/* Append only */
#define S_IMMUTABLE	8	/* Immutable file */
#define S_DAX		16	/* Direct access */
#define S_DIRSYNC	32	/* Directory sync */
#define S_ENCRYPTED	64	/* Encrypted */
#define S_CASEFOLD	128	/* Case-folded */
#define S_VERITY	256	/* Verity enabled */
#define S_NOQUOTA	0	/* No quota (stub) */

/* Open flags - stubs for U-Boot */
#define O_SYNC		0

/* Permission mode constants */
#define S_IRWXUGO	(S_IRWXU | S_IRWXG | S_IRWXO)
#define S_IRUGO		(S_IRUSR | S_IRGRP | S_IROTH)

/* Rename flags */
#define RENAME_NOREPLACE	(1 << 0)
#define RENAME_EXCHANGE		(1 << 1)
#define RENAME_WHITEOUT		(1 << 2)

/* Whiteout device - used for overlayfs */
#define WHITEOUT_DEV		0
#define WHITEOUT_MODE		0

/* Superblock flags */
#define SB_RDONLY	(1 << 0)	/* Read-only mount */
#define SB_POSIXACL	(1 << 16)	/* POSIX ACL support */
#define SB_LAZYTIME	(1 << 25)	/* Lazy time updates */
#define SB_I_VERSION	(1 << 26)	/* Update inode version */
#define SB_INLINECRYPT	(1 << 27)	/* Inline encryption */
#define SB_ACTIVE	(1 << 30)	/* Superblock is active */
#define SB_SILENT	(1 << 15)	/* Silent mount errors */

/* Superblock freeze levels */
#define SB_FREEZE_WRITE		1
#define SB_FREEZE_PAGEFAULT	2
#define SB_FREEZE_FS		3
#define SB_FREEZE_COMPLETE	4

/* Filesystem management time flag */
#define FS_MGTIME		0

/* Block size constant */
#define BLOCK_SIZE		1024

/* VFS position helper */
#define vfs_setpos(file, offset, maxsize) \
	({ (void)(file); (void)(maxsize); (offset); })

/* fallocate() flags */
#define FALLOC_FL_KEEP_SIZE		0x01
#define FALLOC_FL_PUNCH_HOLE		0x02
#define FALLOC_FL_COLLAPSE_RANGE	0x08
#define FALLOC_FL_ZERO_RANGE		0x10
#define FALLOC_FL_INSERT_RANGE		0x20
#define FALLOC_FL_WRITE_ZEROES		0x40
#define FALLOC_FL_ALLOCATE_RANGE	0x80
#define FALLOC_FL_MODE_MASK		0xff

/* Directory entry types */
#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

/* Directory context for readdir iteration */
struct dir_context;
typedef int (*filldir_t)(struct dir_context *, const char *, int, loff_t,
			 u64, unsigned);

struct dir_context {
	filldir_t actor;
	loff_t pos;
};

/* dir_emit - emit a directory entry to the context callback */
static inline bool dir_emit(struct dir_context *ctx, const char *name, int len,
			    u64 ino, unsigned int type)
{
	return ctx->actor(ctx, name, len, ctx->pos, ino, type) == 0;
}

/* dir_emit_dot/dotdot - implemented in fs/linux_fs.c */
bool dir_emit_dot(struct file *file, struct dir_context *ctx);
bool dir_emit_dotdot(struct file *file, struct dir_context *ctx);

#define dir_relax_shared(i)	({ (void)(i); 1; })

/* Inode mutex nesting classes */
enum {
	I_MUTEX_NORMAL,
	I_MUTEX_PARENT,
	I_MUTEX_CHILD,
	I_MUTEX_XATTR,
	I_MUTEX_NONDIR2,
	I_MUTEX_PARENT2,
};

/*
 * Inode locking stubs - U-Boot is single-threaded, no locking needed.
 */
#define inode_lock(inode)		do { (void)(inode); } while (0)
#define inode_unlock(inode)		do { (void)(inode); } while (0)
#define inode_lock_shared(inode)	do { (void)(inode); } while (0)
#define inode_unlock_shared(inode)	do { (void)(inode); } while (0)
#define inode_trylock(inode)		({ (void)(inode); 1; })
#define inode_trylock_shared(inode)	({ (void)(inode); 1; })
#define inode_dio_wait(inode)		do { (void)(inode); } while (0)
#define inode_lock_nested(inode, subclass) \
	do { (void)(inode); (void)(subclass); } while (0)

/*
 * Inode helper functions
 */

/* inode_is_locked - check if inode lock is held (always true in U-Boot) */
#define inode_is_locked(i)	(1)

/* i_size accessors */
#define i_size_write(i, s)	do { (i)->i_size = (s); } while (0)
#define i_size_read(i)		((i)->i_size)

/* i_blocksize - get block size from inode */
#define i_blocksize(i)		(1U << (i)->i_blkbits)

/* inode_newsize_ok - check if new size is valid (always ok in U-Boot) */
#define inode_newsize_ok(i, s)	({ (void)(i); (void)(s); 0; })

/* IS_SYNC, IS_APPEND, IS_IMMUTABLE, IS_CASEFOLDED - inode flag checks */
#define IS_SYNC(inode)		(0)
#define IS_APPEND(inode)	((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode)	((inode)->i_flags & S_IMMUTABLE)
#define IS_CASEFOLDED(inode)	(0)	/* Case-folding not supported */
#define IS_DIRSYNC(inode)	({ (void)(inode); 0; })
#define IS_NOSEC(inode)		(1)	/* No security checks in U-Boot */

/* inode_needs_sync - check if inode needs synchronous writes (always false) */
#define inode_needs_sync(inode)	(0)

/* is_bad_inode - check if inode is marked bad (always false in U-Boot) */
#define is_bad_inode(inode)	(0)

/* inode_is_open_for_write - check if inode has open writers (always false) */
#define inode_is_open_for_write(inode)	({ (void)(inode); 0; })

/* inode_is_dirtytime_only - check if inode has only dirty time (always false) */
#define inode_is_dirtytime_only(inode)	({ (void)(inode); 0; })

/* Inode state bits for i_state field */
#define I_NEW			(1 << 0)
#define I_FREEING		(1 << 1)
#define I_DIRTY_DATASYNC	(1 << 2)
#define I_DIRTY_TIME		(1 << 3)

/* Maximum file size for large files */
#define MAX_LFS_FILESIZE	((loff_t)LLONG_MAX)

/*
 * Inode operation stubs - U-Boot has simplified inode handling
 */
#define icount_read(inode)		(1)
#define i_uid_write(inode, uid)		do { } while (0)
#define i_gid_write(inode, gid)		do { } while (0)
#define inode_fsuid_set(inode, idmap)	do { } while (0)
#define inode_init_owner(idmap, i, dir, mode) \
	do { (i)->i_mode = (mode); } while (0)
#define insert_inode_locked(inode)	(0)
#define unlock_new_inode(inode)		do { } while (0)
#define clear_nlink(inode)		do { } while (0)
#define set_nlink(i, n)			do { (i)->i_nlink = (n); } while (0)
#define inc_nlink(i)			do { (i)->i_nlink++; } while (0)
#define drop_nlink(i)			do { (i)->i_nlink--; } while (0)
#define IS_NOQUOTA(inode)		(0)
#define IS_SWAPFILE(inode)		({ (void)(inode); 0; })
#define inode_set_ctime_current(i)	({ (void)(i); (struct timespec64){}; })
#define inode_set_mtime_to_ts(i, ts)	({ (void)(i); (ts); })
#define inode_set_flags(i, f, m)	do { } while (0)
#define inode_set_cached_link(i, l, len) do { } while (0)
#define init_special_inode(i, m, d)	do { } while (0)
#define make_bad_inode(i)		do { } while (0)
#define iget_failed(i)			do { } while (0)
#define find_inode_by_ino_rcu(sb, ino)	((struct inode *)NULL)
#define mark_inode_dirty(i)		do { } while (0)
#define invalidate_inode_buffers(i)	do { } while (0)
#define clear_inode(i)			do { } while (0)

/* Inode credential helpers */
#define i_uid_needs_update(m, a, i)	({ (void)(m); (void)(a); (void)(i); 0; })
#define i_gid_needs_update(m, a, i)	({ (void)(m); (void)(a); (void)(i); 0; })
#define i_uid_update(m, a, i)		do { } while (0)
#define i_gid_update(m, a, i)		do { } while (0)

/* lock_two_nondirectories - lock two inodes in order */
#define lock_two_nondirectories(i1, i2) \
	do { (void)(i1); (void)(i2); } while (0)
#define unlock_two_nondirectories(i1, i2) \
	do { (void)(i1); (void)(i2); } while (0)

/* Inode allocation and lifecycle - implemented in ext4l */
struct kmem_cache;
void *alloc_inode_sb(struct super_block *sb, struct kmem_cache *cache,
		     gfp_t gfp);
int inode_generic_drop(struct inode *inode);
struct inode *new_inode(struct super_block *sb);
struct inode *iget_locked(struct super_block *sb, unsigned long ino);
void iput(struct inode *inode);
#define ihold(i)			do { (void)(i); } while (0)

/* Block mapping - implemented in ext4l/stub.c */
int bmap(struct inode *inode, sector_t *block);

/* Simple filesystem helpers */
#define simple_open(i, f)		({ (void)(i); (void)(f); 0; })
#define finish_open_simple(f, e)	(e)

/* simple_get_link - for fast symlinks stored in inode */
struct delayed_call;
static inline const char *simple_get_link(struct dentry *dentry,
					  struct inode *inode,
					  struct delayed_call *callback)
{
	return inode->i_link;
}

/**
 * get_block_t - block mapping callback type
 * @inode: inode to map blocks for
 * @iblock: logical block number
 * @bh_result: buffer head to fill with mapping
 * @create: whether to create new blocks
 *
 * Callback function type for filesystem block mapping.
 */
typedef int (get_block_t)(struct inode *inode, sector_t iblock,
			  struct buffer_head *bh_result, int create);

/**
 * struct fstrim_range - fstrim ioctl argument
 * @start: first byte to trim
 * @len: number of bytes to trim
 * @minlen: minimum extent length
 *
 * Used for FITRIM ioctl to trim unused blocks.
 */
struct fstrim_range {
	u64 start;
	u64 len;
	u64 minlen;
};

/* Forward declarations for file/inode operations */
struct delayed_call;
struct fiemap_extent_info;
struct file_kattr;
struct posix_acl;
struct mnt_idmap;
struct kstat;

/**
 * struct file_operations - filesystem file operations
 *
 * Methods for file I/O and directory iteration.
 */
struct file_operations {
	int (*open)(struct inode *, struct file *);
	loff_t (*llseek)(struct file *, loff_t, int);
	ssize_t (*read)(struct file *, char *, size_t, loff_t *);
	int (*iterate_shared)(struct file *, struct dir_context *);
	long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
	int (*fsync)(struct file *, loff_t, loff_t, int);
	int (*release)(struct inode *, struct file *);
};

/**
 * struct inode_operations - filesystem inode operations
 *
 * Methods for inode manipulation, including symlinks and directories.
 */
struct inode_operations {
	/* Symlink operations */
	const char *(*get_link)(struct dentry *, struct inode *,
				struct delayed_call *);
	/* Common operations */
	int (*getattr)(struct mnt_idmap *, const struct path *,
		       struct kstat *, u32, unsigned int);
	ssize_t (*listxattr)(struct dentry *, char *, size_t);
	int (*fiemap)(struct inode *, struct fiemap_extent_info *, u64, u64);
	int (*setattr)(struct mnt_idmap *, struct dentry *, struct iattr *);
	struct posix_acl *(*get_inode_acl)(struct inode *, int, bool);
	int (*set_acl)(struct mnt_idmap *, struct dentry *,
		       struct posix_acl *, int);
	int (*fileattr_get)(struct dentry *, struct file_kattr *);
	int (*fileattr_set)(struct mnt_idmap *, struct dentry *,
			    struct file_kattr *);
	/* Directory operations */
	struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
	int (*create)(struct mnt_idmap *, struct inode *, struct dentry *,
		      umode_t, bool);
	int (*link)(struct dentry *, struct inode *, struct dentry *);
	int (*unlink)(struct inode *, struct dentry *);
	int (*symlink)(struct mnt_idmap *, struct inode *, struct dentry *,
		       const char *);
	struct dentry *(*mkdir)(struct mnt_idmap *, struct inode *,
				struct dentry *, umode_t);
	int (*rmdir)(struct inode *, struct dentry *);
	int (*mknod)(struct mnt_idmap *, struct inode *, struct dentry *,
		     umode_t, dev_t);
	int (*rename)(struct mnt_idmap *, struct inode *, struct dentry *,
		      struct inode *, struct dentry *, unsigned int);
	int (*tmpfile)(struct mnt_idmap *, struct inode *, struct file *,
		       umode_t);
};

/*
 * Generic VFS operations - stubs for U-Boot
 */

/* Generic file I/O operations */
#define generic_file_read_iter(iocb, to)	({ (void)(iocb); (void)(to); 0L; })
#define generic_write_checks(iocb, from)	({ (void)(iocb); (void)(from); 0L; })
#define generic_perform_write(iocb, from)	({ (void)(iocb); (void)(from); 0L; })
#define generic_write_sync(iocb, count)		({ (void)(iocb); (count); })
#define generic_atomic_write_valid(iocb, from)	({ (void)(iocb); (void)(from); 0; })

/* Generic fsync operation */
#define generic_buffers_fsync_noflush(f, s, e, d) \
	({ (void)(f); (void)(s); (void)(e); (void)(d); 0; })

/* Sync operations - stubs */
#define sync_mapping_buffers(m)		({ (void)(m); 0; })
#define sync_inode_metadata(i, w)	({ (void)(i); (void)(w); 0; })

/* Generic attribute operations */
#define generic_fillattr(m, req, i, s)	do { } while (0)
#define generic_fill_statx_atomic_writes(s, u_m, u_M, g) do { } while (0)

/* Generic seek operation */
#define generic_file_llseek_size(f, o, w, m, e) \
	({ (void)(f); (void)(o); (void)(w); (void)(m); (void)(e); 0LL; })

/* Case-insensitive name validation - not supported */
#define generic_ci_validate_strict_name(d, n)	({ (void)(d); (void)(n); 1; })

/* Generic directory read - implemented in fs/linux_fs.c */
ssize_t generic_read_dir(struct file *f, char __user *buf, size_t count,
			 loff_t *ppos);

/* Block addressability check - implemented in fs/linux_fs.c */
int generic_check_addressable(unsigned int blocksize_bits, u64 num_blocks);

/* Read-only file operations stub - implemented in fs/linux_fs.c */
extern const struct file_operations generic_ro_fops;

/* Symlink inode operations stub - implemented in fs/linux_fs.c */
extern const struct inode_operations page_symlink_inode_operations;

#endif /* _LINUX_FS_H */
