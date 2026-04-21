// SPDX-License-Identifier: GPL-2.0+
/*
 * Stub functions for ext4l filesystem
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 *
 * These stubs allow the ext4l code to link during development while not all
 * source files are present. They will be removed once the full ext4l
 * implementation is complete.
 *
 * DO NOT use this file as a reference - these are temporary placeholders only.
 */

#include "ext4_uboot.h"
#include <linux/types.h>

struct super_block;
struct buffer_head;
struct inode;
struct ext4_map_blocks;
struct file;

/* bh_end_io_t - buffer head end io callback */
typedef void bh_end_io_t(struct buffer_head *bh, int uptodate);

/* ext4_num_base_meta_blocks and ext4_get_group_desc are now in balloc.c */
/* ext4_block_bitmap is now in super.c */
/* ext4_inode_bitmap is now in super.c */
/* ext4_inode_table is now in super.c */
/* __ext4_error_inode is now in super.c */
/* __ext4_error is now in super.c */
/* __ext4_std_error is now in super.c */
/* ext4_decode_error is now in super.c */

/*
 * JBD2 journal stubs - most now in transaction.c, journal.c, revoke.c
 */
struct jbd2_journal_handle;
typedef struct jbd2_journal_handle handle_t;
struct journal_s;
typedef struct journal_s journal_t;

/* jbd2__journal_start is now in transaction.c */
/* jbd2_journal_stop is now in transaction.c */
/* jbd2_journal_free_reserved is now in transaction.c */
/* jbd2_journal_start_reserved is now in transaction.c */
/* jbd2_journal_extend is now in transaction.c */
/* jbd2_journal_get_write_access is now in transaction.c */
/* jbd2_journal_set_triggers is now in transaction.c */
/* jbd2_journal_forget is now in transaction.c */
/* jbd2_journal_revoke is now in revoke.c */
/* jbd2_journal_get_create_access is now in transaction.c */
/* jbd2_journal_dirty_metadata is now in transaction.c */
/* jbd2_journal_force_commit_nested is now in journal.c */
/* jbd2__journal_restart is now in transaction.c */
/* jbd2_trans_will_send_data_barrier is now in journal.c */

/*
 * Stubs for balloc.c
 */
/* ext4_mark_group_bitmap_corrupted is now in super.c */
/* __ext4_warning is now in super.c */

/* ext4_mb_new_blocks is now in mballoc.c */

/* ext4_free_group_clusters is now in super.c */
/* ext4_clear_inode is now in super.c */
/* __ext4_msg is now in super.c */
/* ext4_free_group_clusters_set is now in super.c */
/* ext4_group_desc_csum_set is now in super.c */
/* ext4_itable_unused_count is now in super.c */
/* ext4_itable_unused_set is now in super.c */
/* ext4_free_inodes_count is now in super.c */
/* ext4_free_inodes_set is now in super.c */
/* ext4_used_dirs_count is now in super.c */

/*
 * Bit operations - sandbox declares these extern but doesn't implement them.
 * These work on bitmaps where nr is the absolute bit number.
 */
void set_bit(int nr, volatile void *addr)
{
	unsigned long *p = (unsigned long *)addr + (nr / BITS_PER_LONG);

	*p |= (1UL << (nr % BITS_PER_LONG));
}

void clear_bit(int nr, volatile void *addr)
{
	unsigned long *p = (unsigned long *)addr + (nr / BITS_PER_LONG);

	*p &= ~(1UL << (nr % BITS_PER_LONG));
}

void change_bit(int nr, volatile void *addr)
{
	unsigned long *p = (unsigned long *)addr + (nr / BITS_PER_LONG);

	*p ^= (1UL << (nr % BITS_PER_LONG));
}

/*
 * Stubs for extents.c
 */
struct ext4_sb_info;
struct ext4_es_tree;
struct extent_status;

/* ext4_es_cache_extent is now in extents_status.c */

/* ext4_es_insert_extent is now in extents_status.c */

/* ext4_remove_pending is now in extents_status.c */

/* ext4_free_blocks is now in mballoc.c */

/* ext4_discard_preallocations is now in mballoc.c */

/* ext4_is_pending is now in extents_status.c */
/* ext4_convert_inline_data is now in inline.c */

/* ext4_fc_mark_ineligible is now in fast_commit.c */

/* ext4_es_lookup_extent is now in extents_status.c */

/* ext4_es_remove_extent is now in extents_status.c */

/* ext4_es_find_extent_range is now in extents_status.c */

/* ext4_mb_mark_bb is now in mballoc.c */

/* ext4_fc_record_regions is now in fast_commit.c */

/* ext4_fc_replay_check_excluded is now in fast_commit.c */

/* jbd2_submit_inode_data is now in commit.c */
/* jbd2_wait_inode_data is now in commit.c */
/* jbd2_fc_get_buf is now in journal.c */
/* jbd2_fc_release_bufs is now in journal.c */
/* jbd2_fc_begin_commit is now in journal.c */
/* jbd2_fc_end_commit is now in journal.c */
/* jbd2_fc_end_commit_fallback is now in journal.c */
/* jbd2_fc_wait_bufs is now in journal.c */
/* jbd2_complete_transaction is now in journal.c */

void ext4_reset_inode_seed(struct inode *inode)
{
}

/*
 * Stubs for page-io.c
 */
bool __folio_start_writeback(struct folio *folio, bool keep_write)
{
	return false;
}

/* ext4_read_bh is now in super.c */
/* ext4_sb_bread_nofail is now in super.c */

/* ext4_init_security stub is provided by xattr.h */
/* xattr functions are now in xattr.c */

/*
 * Stubs for orphan.c
 */
struct ext4_iloc;

/* ext4_superblock_csum_set is now in super.c */
/* ext4_feature_set_ok is now in super.c */

/*
 * Stubs for inode.c
 */
#include <linux/sched.h>

/* jbd2_journal_blocks_per_folio is now in journal.c */
/* jbd2_transaction_committed is now in journal.c */


/* __ext4_warning_inode is now in super.c */

/* ext4_mpage_readpages is now in readpage.c */

/* ext4_readpage_inline is now in inline.c */

/* Xattr functions are now in xattr.c */

/* jbd2_journal_inode_ranged_write is now in transaction.c */
/* ext4_read_bh_lock is now in super.c */
/* ext4_fc_commit is now in fast_commit.c */
/* ext4_force_commit is now in super.c */
/* Inline data is now in inline.c */
/* I/O submit stubs are now in page-io.c */
/* jbd2_journal_begin_ordered_truncate is now in transaction.c */
/* jbd2_journal_invalidate_folio is now in transaction.c */
/* jbd2_log_wait_commit is now in journal.c */
/* ext4_fc_track_range is now in fast_commit.c */
/* jbd2_journal_lock_updates is now in transaction.c */
/* jbd2_journal_unlock_updates is now in transaction.c */
/* jbd2_journal_flush is now in journal.c */
/* ext4_fc_track_inode is now in fast_commit.c */
/* ext4_fc_init_inode is now in fast_commit.c */
/* jbd2_journal_inode_ranged_wait is now in transaction.c */

/* Inline data functions are now in inline.c */

/* __xattr_check_inode is now in xattr.c */

/* File and inode operations symbols */
/* ext4_file_inode_operations is now in file.c */
/* ext4_file_operations is now in file.c */
/* ext4_dir_inode_operations is now in namei.c */
/* ext4_dir_operations is now in dir.c */
/* ext4_special_inode_operations is now in namei.c */
/* ext4_symlink_inode_operations is now in symlink.c */
/* ext4_fast_symlink_inode_operations is now in symlink.c */


/* ext4_update_dynamic_rev is now in super.c */

/* Inline data stubs are now in inline.c */

/* xattr stubs are now in xattr.c */

/* jbd2_inode_cache is now in journal.c */
/* jbd2_journal_try_to_free_buffers is now in transaction.c */
/* jbd2_journal_init_jbd_inode is now in journal.c */

/* ext4_read_inline_link is now in inline.c */

/*
 * Stubs for dir.c
 */
/* generic_read_dir is now in fs/linux_fs.c */

/* __ext4_error_file is now in super.c */

/* ext4_llseek is now in file.c */

/* ext4_htree_fill_tree is now in namei.c */

/* Inline dir stubs are now in inline.c */

/* Fast commit stubs are now in fast_commit.c */

/* fileattr stubs */
int ext4_fileattr_get(struct dentry *dentry, void *fa)
{
	return 0;
}

int ext4_fileattr_set(void *idmap, struct dentry *dentry, void *fa)
{
	return 0;
}

/* ext4_dirblock_csum_verify is now in namei.c */

/* ext4_ioctl is now in super.c */

/* ext4_sync_file is now in fsync.c */

/*
 * Stubs for super.c
 */

/* fscrypt stubs */
void fscrypt_free_dummy_policy(struct fscrypt_dummy_policy *policy)
{
}

int fscrypt_is_dummy_policy_set(const struct fscrypt_dummy_policy *policy)
{
	return 0;
}

int fscrypt_dummy_policies_equal(const struct fscrypt_dummy_policy *p1,
				 const struct fscrypt_dummy_policy *p2)
{
	return 1;
}

void fscrypt_show_test_dummy_encryption(struct seq_file *seq, char sep,
					struct super_block *sb)
{
}

void fscrypt_free_inode(struct inode *inode)
{
}

int fscrypt_drop_inode(struct inode *inode)
{
	return 0;
}

/* Block device stubs */
void bdev_fput(void *file)
{
}

void *bdev_file_open_by_dev(dev_t dev, int flags, void *holder,
			    const struct blk_holder_ops *ops)
{
	return ERR_PTR(-ENODEV);
}

/* bdev_getblk implemented in interface.c */

int trylock_buffer(struct buffer_head *bh)
{
	return 1;
}

/* submit_bh implemented in interface.c */

/* NFS export stubs */
struct dentry *generic_fh_to_parent(struct super_block *sb, struct fid *fid,
				    int fh_len, int fh_type,
				    struct inode *(*get_inode)(struct super_block *, u64, u32))
{
	return ERR_PTR(-ESTALE);
}

struct dentry *generic_fh_to_dentry(struct super_block *sb, struct fid *fid,
				    int fh_len, int fh_type,
				    struct inode *(*get_inode)(struct super_block *, u64, u32))
{
	return ERR_PTR(-ESTALE);
}

/* Inode stubs */
int inode_generic_drop(struct inode *inode)
{
	return 0;
}

/* alloc_inode_sb() is now in fs/linux_fs.c */

/* inode_set_iversion is now a macro in linux/iversion.h */

/* rwlock_init is now a macro in linux/spinlock.h */

/* trace_ext4_drop_inode is now a macro in ext4_uboot.h */

/* Shutdown stub */
void ext4_force_shutdown(void *sb, int flags)
{
}

/* Memory stubs */
void *kvzalloc(size_t size, gfp_t flags)
{
	return calloc(1, size);
}

/* ext4_kvfree_array_rcu - now in resize.c */

/* ext4_update_overhead - stub for resize.c */
int ext4_update_overhead(struct super_block *sb, bool force)
{
	return 0;
}

/* strtomem_pad, strscpy_pad, strreplace are now in linux/string.h */
/* kmemdup_nul is now in linux/slab.h, with implementation in lib/string.c */

/* Page allocation */
unsigned long get_zeroed_page(gfp_t gfp)
{
	void *p = memalign(4096, 4096);

	if (p)
		memset(p, 0, 4096);
	return (unsigned long)p;
}

/* free_page() is now in fs/linux_fs.c */

/* Trace stubs */
void trace_ext4_error(struct super_block *sb, const char *func, unsigned int line)
{
}

/* Rate limiting */
int ___ratelimit(struct ratelimit_state *rs, const char *func)
{
	return 1;
}

/* I/O priority stubs are now in linux/ioprio.h */

/* ext4_fc_init is now in fast_commit.c */

/* Filesystem sync */
int sync_filesystem(void *sb)
{
	return 0;
}

/* dquot_suspend is now a macro in ext4_uboot.h */

/* MMP daemon - now in mmp.c */

/* Sysfs */
void ext4_unregister_sysfs(void *sb)
{
}

/* jbd2_journal_destroy is now in journal.c */

/* percpu_free_rwsem is now in linux/percpu.h */

/* Block device ops */
int sync_blockdev(struct block_device *bdev)
{
	return 0;
}

void invalidate_bdev(struct block_device *bdev)
{
}

int bdev_read_only(struct block_device *bdev)
{
	return bdev ? bdev->read_only : 0;
}

struct block_device *file_bdev(struct file *file)
{
	return NULL;
}

/* xattr cache is now in xattr.c */

/* kobject */
void kobject_put(struct kobject *kobj)
{
}

/* completion - now uses linux/completion.h macro */

/* DAX */
void *fs_dax_get_by_bdev(struct block_device *bdev, u64 *start, u64 *len,
			 void *holder)
{
	return NULL;
}

void fs_put_dax(void *dax, void *holder)
{
}

/* sb_set_blocksize() is now in fs/linux_fs.c */

/* strscpy_pad is now a macro in linux/string.h */
/* kmemdup_nul is now in lib/string.c */

/* generic_check_addressable is now in fs/linux_fs.c */

/* Block device blocks */
u64 sb_bdev_nr_blocks(struct super_block *sb)
{
	return 0;
}

/* bgl_lock_init is now a macro in ext4_uboot.h */

/* xattr handlers are now in xattr.c */

/* super_set_uuid is now a macro in ext4_uboot.h */
/* super_set_sysfs_name_bdev is now a macro in ext4_uboot.h */
/* bdev_can_atomic_write is now a macro in ext4_uboot.h */
/* bdev_atomic_write_unit_max_bytes is now a macro in ext4_uboot.h */

/* Multi-mount protection - now in mmp.c */

/* Generic dentry ops */
void generic_set_sb_d_ops(struct super_block *sb)
{
}

/* d_make_root() and iput() are now in fs/linux_fs.c */

/* percpu_init_rwsem is now in linux/percpu.h */

/* atomic_add and atomic64_add are now in asm-generic/atomic.h */

/* Discard */
unsigned int bdev_max_discard_sectors(struct block_device *bdev)
{
	return 0;
}

/* Rate limit init */
void ratelimit_state_init(void *rs, int interval, int burst)
{
}

/* Sysfs */
int ext4_register_sysfs(void *sb)
{
	return 0;
}

/* dput - now provided as macro in ext4_uboot.h */

/* timer_delete_sync is now a macro in linux/timer.h */

/* ext4_get_parent is now in namei.c */

/* fsnotify */
void fsnotify_sb_error(struct super_block *sb, struct inode *inode, int error)
{
}

/* jbd2_journal_force_commit is now in journal.c */

/* File path */
char *file_path(struct file *file, char *buf, int buflen)
{
	return buf;
}

/* ext4_fc_del is now in fast_commit.c */

/* invalidate_inode_buffers is now a macro in ext4_uboot.h */
/* clear_inode is now a macro in ext4_uboot.h */
/* fscrypt_put_encryption_info is now a macro in ext4_uboot.h */
/* fsverity_cleanup_inode is now a macro in ext4_uboot.h */

/* ext4_ioctl - file ioctls not supported in U-Boot */
long ext4_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOTSUPP;
}

/* jbd2_journal_abort is now in journal.c */

/* jbd2_journal_release_jbd_inode is now in journal.c */

/* nop_mnt_idmap - no-op mount ID map for xattr.c */
struct mnt_idmap nop_mnt_idmap;

/* Quota stubs are now macros in linux/quotaops.h */

/*
 * JBD2 stubs - temporary stubs until other jbd2 files are added
 * Note: These use void* to avoid pulling in jbd2.h types which would conflict
 */

/* jbd2_journal_get_log_tail is now in journal.c */
/* __jbd2_update_log_tail is now in journal.c */
/* jbd2_journal_grab_journal_head is now in journal.c */
/* jbd2_journal_put_journal_head is now in journal.c */
/* jbd2_journal_free_transaction is now in transaction.c */
/* jbd2_log_start_commit is now in journal.c */
/* jbd2_journal_get_descriptor_buffer is now in journal.c */
/* jbd2_journal_update_sb_log_tail is now in journal.c */
/* jbd2_free is now in journal.c */
/* journal_tag_bytes is now in journal.c */
/* jbd2_journal_wait_updates is now in transaction.c */
/* jbd2_journal_refile_buffer is now in transaction.c */
/* jbd2_clear_buffer_revoked_flags is now in revoke.c */
/* jbd2_journal_switch_revoke_table is now in revoke.c */
/* jbd2_journal_write_revoke_records is now in revoke.c */
/* jbd2_buffer_abort_trigger is now in transaction.c */
/* jbd2_journal_next_log_block is now in journal.c */
/* jbd2_journal_write_metadata_buffer is now in journal.c */
/* jbd2_descriptor_block_csum_set is now in journal.c */
/* jbd2_update_log_tail is now in journal.c */
/* jbd2_journal_file_buffer is now in transaction.c */
/* __jbd2_journal_refile_buffer is now in transaction.c */
/* cond_resched_lock is now a macro in ext4_uboot.h */
/* jbd2_journal_recover is now in recovery.c */
/* jbd2_journal_skip_recovery is now in recovery.c */
/* jbd2_journal_destroy_revoke is now in revoke.c */
/* jbd2_journal_init_revoke_table is now in revoke.c */
/* jbd2_journal_test_revoke is now in revoke.c */
/* jbd2_journal_set_revoke is now in revoke.c */
/* jbd2_journal_clear_revoke is now in revoke.c */
/* jbd2_journal_destroy_revoke_table is now in revoke.c */
/* jbd2_buffer_frozen_trigger is now in transaction.c */
/* __jbd2_journal_file_buffer is now in transaction.c */
