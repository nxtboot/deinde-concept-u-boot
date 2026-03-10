/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * R/O (V)FAT 12/16/32 filesystem implementation by Marcus Sundberg
 *
 * 2002-07-28 - rjones@nexus-tech.net - ported to ppcboot v1.1.6
 * 2003-03-10 - kharris@nexus-tech.net - ported to u-boot
 */

#ifndef _FAT_H_
#define _FAT_H_

#include <fs_legacy.h>
#include <asm/byteorder.h>

struct disk_partition;

/* Maximum Long File Name length supported here is 128 UTF-16 code units */
#define VFAT_MAXLEN_BYTES	256 /* Maximum LFN buffer in bytes */
#define VFAT_MAXSEQ		9   /* Up to 9 of 13 2-byte UTF-16 entries */
#define PREFETCH_BLOCKS		2

#define MAX_CLUSTSIZE	CONFIG_FS_FAT_MAX_CLUSTSIZE

#define DIRENTSPERCLUST	((mydata->clust_size * mydata->sect_size) / \
			 sizeof(dir_entry))

#define FATBUFBLOCKS	6
#define FATBUFSIZE	(mydata->sect_size * FATBUFBLOCKS)
#define FAT12BUFSIZE	((FATBUFSIZE*2)/3)
#define FAT16BUFSIZE	(FATBUFSIZE/2)
#define FAT32BUFSIZE	(FATBUFSIZE/4)

/* Maximum number of entry for long file name according to spec */
#define MAX_LFN_SLOT	20

/* File attributes */
#define ATTR_RO	1
#define ATTR_HIDDEN	2
#define ATTR_SYS	4
#define ATTR_VOLUME	8
#define ATTR_DIR	16
#define ATTR_ARCH	32

#define ATTR_VFAT	(ATTR_RO | ATTR_HIDDEN | ATTR_SYS | ATTR_VOLUME)

#define DELETED_FLAG	((char)0xe5) /* Marks deleted files when in name[0] */
#define aRING		0x05	     /* Used as special character in name[0] */

/*
 * Indicates that the entry is the last long entry in a set of long
 * dir entries
 */
#define LAST_LONG_ENTRY_MASK	0x40

#define ISDIRDELIM(c)	((c) == '/' || (c) == '\\')

#define FSTYPE_NONE	(-1)

#if defined(__linux__) && defined(__KERNEL__)
#define FAT2CPU16	le16_to_cpu
#define FAT2CPU32	le32_to_cpu
#else
#if __LITTLE_ENDIAN
#define FAT2CPU16(x)	(x)
#define FAT2CPU32(x)	(x)
#else
#define FAT2CPU16(x)	((((x) & 0x00ff) << 8) | (((x) & 0xff00) >> 8))
#define FAT2CPU32(x)	((((x) & 0x000000ff) << 24)  |	\
			 (((x) & 0x0000ff00) << 8)  |	\
			 (((x) & 0x00ff0000) >> 8)  |	\
			 (((x) & 0xff000000) >> 24))
#endif
#endif

#define START(dent)	(FAT2CPU16((dent)->start) \
			+ (mydata->fatsize != 32 ? 0 : \
			  (FAT2CPU16((dent)->starthi) << 16)))
#define IS_LAST_CLUST(x, fatsize) ((x) >= ((fatsize) != 32 ? \
					((fatsize) != 16 ? 0xff8 : 0xfff8) : \
					0xffffff8))
#define CHECK_CLUST(x, fatsize) ((x) <= 1 || \
				(x) >= ((fatsize) != 32 ? \
					((fatsize) != 16 ? 0xff0 : 0xfff0) : \
					0xffffff0))

/**
 * struct boot_sector - FAT boot sector structure
 * @ignored: bootstrap code (first 3 bytes)
 * @system_id: name of filesystem (8 bytes)
 * @sector_size: bytes per sector
 * @cluster_size: sectors per cluster
 * @reserved: number of reserved sectors
 * @fats: number of FAT copies
 * @dir_entries: number of root directory entries
 * @sectors: number of sectors (for small disks)
 * @media: media descriptor code
 * @fat_length: sectors per FAT (for FAT12/16)
 * @secs_track: sectors per track
 * @heads: number of heads
 * @hidden: number of hidden sectors
 * @total_sect: total number of sectors (for larger disks)
 * @fat32_length: sectors per FAT (FAT32 only)
 * @flags: flags (bit 8: fat mirroring, low 4: active fat)
 * @version: filesystem version (FAT32 only)
 * @root_cluster: first cluster of root directory (FAT32 only)
 * @info_sector: filesystem info sector (FAT32 only)
 * @backup_boot: backup boot sector location (FAT32 only)
 * @reserved2: unused (FAT32 only)
 */
struct boot_sector {
	u8	ignored[3];
	char	system_id[8];
	u8	sector_size[2];
	u8	cluster_size;
	u16	reserved;
	u8	fats;
	u8	dir_entries[2];
	u8	sectors[2];
	u8	media;
	u16	fat_length;
	u16	secs_track;
	u16	heads;
	u32	hidden;
	u32	total_sect;

	/* FAT32 only */
	u32	fat32_length;
	u16	flags;
	u8	version[2];
	u32	root_cluster;
	u16	info_sector;
	u16	backup_boot;
	u16	reserved2[6];
};

/**
 * struct volume_info - FAT volume information structure
 * @drive_number: BIOS drive number
 * @reserved: unused field
 * @ext_boot_sign: extended boot signature (0x29 if fields below exist)
 * @volume_id: volume serial number (4 bytes)
 * @volume_label: volume label (11 bytes, padded with spaces)
 * @fs_type: filesystem type string (typically "FAT12", "FAT16", or "FAT32")
 *
 * This structure is part of the boot sector, located after the common fields.
 * Boot code follows this structure, with boot signature at the end of sector.
 */
struct volume_info {
	u8 drive_number;
	u8 reserved;
	u8 ext_boot_sign;
	u8 volume_id[4];
	char volume_label[11];
	char fs_type[8];
};

/* see dir_entry::lcase: */
#define CASE_LOWER_BASE	8	/* base (name) is lower case */
#define CASE_LOWER_EXT	16	/* extension is lower case */

/**
 * struct nameext - 8.3 filename components
 * @name: filename (8 bytes)
 * @ext: extension (3 bytes)
 */
struct nameext {
	char name[8];
	char ext[3];
};

/**
 * struct dir_entry - FAT directory entry
 * @nameext: filename and extension (8.3 format)
 * @attr: file attributes (ATTR_* flags)
 * @lcase: case flags for name and extension (CASE_LOWER_* flags)
 * @ctime_ms: creation time (milliseconds)
 * @ctime: creation time (hours, minutes, seconds)
 * @cdate: creation date
 * @adate: last access date
 * @starthi: high 16 bits of cluster number (FAT32 only)
 * @time: modification time
 * @date: modification date
 * @start: low 16 bits of cluster number
 * @size: file size in bytes
 */
struct dir_entry {
	struct nameext nameext;
	u8	attr;
	u8	lcase;
	u8	ctime_ms;
	u16	ctime;
	u16	cdate;
	u16	adate;
	u16	starthi;
	u16	time;
	u16	date;
	u16	start;
	u32	size;
};

/**
 * struct dir_slot - VFAT long filename entry
 * @id: sequence number (bit 6 = last entry, bits 0-4 = sequence)
 * @name0_4: characters 0-4 of long filename (UTF-16LE)
 * @attr: must be ATTR_VFAT (0x0F)
 * @reserved: unused field
 * @alias_checksum: checksum of 8.3 alias for this long name
 * @name5_10: characters 5-10 of long filename (UTF-16LE)
 * @start: unused (always 0)
 * @name11_12: characters 11-12 of long filename (UTF-16LE)
 *
 * Long filename entries precede the corresponding short entry in directory.
 * Multiple entries may be used to store names longer than 13 characters.
 */
struct dir_slot {
	u8	id;
	u8	name0_4[10];
	u8	attr;
	u8	reserved;
	u8	alias_checksum;
	u8	name5_10[12];
	u16	start;
	u8	name11_12[4];
};

/**
 * struct fsdata - FAT filesystem instance data
 * @fatbuf: buffer for reading/writing FAT (must be 32-bit aligned for FAT32)
 * @fatsize: size of FAT in bits (12, 16, or 32)
 * @fatlength: length of FAT in sectors
 * @fat_sect: starting sector of the FAT
 * @fat_dirty: flag indicating if fatbuf has been modified
 * @rootdir_sect: starting sector of root directory
 * @sect_size: size of sectors in bytes
 * @clust_size: size of clusters in sectors
 * @data_begin: sector offset of first data cluster (can be negative)
 * @fatbufnum: currently buffered FAT sector number (init to -1)
 * @rootdir_size: size of root directory in entries (for non-FAT32)
 * @root_cluster: first cluster of root directory (FAT32 only)
 * @total_sect: total number of sectors
 * @fats: number of FAT copies
 *
 * This structure holds the runtime state of a mounted FAT filesystem.
 * The fatbuf must be 32-bit aligned due to FAT32 sector access requirements.
 */
struct fsdata {
	u8	*fatbuf;
	int	fatsize;
	u32	fatlength;
	u16	fat_sect;
#ifdef CONFIG_FAT_WRITE
	u8	fat_dirty;
#endif
	u32	rootdir_sect;
	u16	sect_size;
	u16	clust_size;
	int	data_begin;
	int	fatbufnum;
	int	rootdir_size;
	u32	root_cluster;
	u32	total_sect;
	int	fats;
};

struct fat_itr;

/**
 * clust_to_sect() - convert cluster number to sector number
 * @fsdata: filesystem instance data
 * @clust: cluster number
 *
 * Return: sector number corresponding to the given cluster
 */
static inline u32 clust_to_sect(struct fsdata *fsdata, u32 clust)
{
	return fsdata->data_begin + clust * fsdata->clust_size;
}

/**
 * sect_to_clust() - convert sector number to cluster number
 * @fsdata: filesystem instance data
 * @sect: sector number
 *
 * Return: cluster number corresponding to the given sector
 */
static inline u32 sect_to_clust(struct fsdata *fsdata, int sect)
{
	return (sect - fsdata->data_begin) / fsdata->clust_size;
}

/**
 * fat_mark_clean() - clear the dirty flag on FAT buffer
 * @fsdata: filesystem instance data
 */
static inline void fat_mark_clean(struct fsdata *fsdata)
{
#ifdef CONFIG_FAT_WRITE
	fsdata->fat_dirty = 0;
#endif
}

/**
 * fat_mark_dirty() - set the dirty flag on FAT buffer
 * @fsdata: filesystem instance data
 */
static inline void fat_mark_dirty(struct fsdata *fsdata)
{
#ifdef CONFIG_FAT_WRITE
	fsdata->fat_dirty = 1;
#endif
}

/**
 * fat_is_dirty() - check if FAT buffer has been modified
 * @fsdata: filesystem instance data
 *
 * Return: true if dirty, false otherwise
 */
static inline bool fat_is_dirty(struct fsdata *fsdata)
{
#ifdef CONFIG_FAT_WRITE
	if (fsdata->fat_dirty)
		return true;
#endif

	return false;
}

/**
 * file_fat_detectfs() - detect and initialize the FAT filesystem
 *
 * Return: 0 on success, -1 on error
 */
int file_fat_detectfs(void);

/**
 * fat_exists() - check if a file exists
 * @filename: full path to file
 *
 * Return: 0 if file exists, -1 if not found or error
 */
int fat_exists(const char *filename);

/**
 * fat_size() - get the size of a file
 * @filename: full path to file
 * @size: pointer to store file size
 *
 * Return: 0 on success, -1 on error
 */
int fat_size(const char *filename, loff_t *size);

/**
 * file_fat_read() - read a file from FAT filesystem
 * @filename: full path to file
 * @buffer: buffer to read data into
 * @maxsize: maximum number of bytes to read
 *
 * Return: number of bytes read, -1 on error
 */
int file_fat_read(const char *filename, void *buffer, int maxsize);

/**
 * fat_set_blk_dev() - set the block device and partition for FAT operations
 * @rbdd: block device descriptor
 * @info: partition information
 *
 * Return: 0 on success, -1 on error
 */
int fat_set_blk_dev(struct blk_desc *rbdd, struct disk_partition *info);

/**
 * fat_register_device() - register a FAT filesystem on a block device
 * @dev_desc: block device descriptor
 * @part_no: partition number (0 = whole device)
 *
 * Return: 0 on success, -1 on error
 */
int fat_register_device(struct blk_desc *dev_desc, int part_no);

/**
 * file_fat_write() - write to a file on FAT filesystem
 * @filename: full path to file
 * @buf: buffer containing data to write
 * @offset: offset in file to start writing
 * @len: number of bytes to write
 * @actwrite: pointer to store actual number of bytes written
 *
 * Return: 0 on success, -1 on error
 */
int file_fat_write(const char *filename, void *buf, loff_t offset, loff_t len,
		   loff_t *actwrite);

/**
 * fat_read_file() - read from a file on FAT filesystem
 * @filename: full path to file
 * @buf: buffer to read data into
 * @offset: offset in file to start reading
 * @len: number of bytes to read
 * @actread: pointer to store actual number of bytes read
 *
 * Return: 0 on success, -1 on error
 */
int fat_read_file(const char *filename, void *buf, loff_t offset, loff_t len,
		  loff_t *actread);

/**
 * fat_opendir() - open a directory for reading
 * @filename: full path to directory
 * @dirsp: pointer to store directory stream handle
 *
 * Return: 0 on success, -1 on error
 */
int fat_opendir(const char *filename, struct fs_dir_stream **dirsp);

/**
 * fat_readdir() - read next entry from directory
 * @dirs: directory stream handle
 * @dentp: pointer to store directory entry
 *
 * Return: 0 on success, -1 on error or end of directory
 */
int fat_readdir(struct fs_dir_stream *dirs, struct fs_dirent **dentp);

/**
 * fat_closedir() - close a directory stream
 * @dirs: directory stream handle
 */
void fat_closedir(struct fs_dir_stream *dirs);

/**
 * fat_unlink() - delete a file or empty directory
 * @filename: full path to file or directory
 *
 * Return: 0 on success, -1 on error
 */
int fat_unlink(const char *filename);

/**
 * fat_rename() - rename a file or directory
 * @old_path: current full path
 * @new_path: new full path
 *
 * Return: 0 on success, -1 on error
 */
int fat_rename(const char *old_path, const char *new_path);

/**
 * fat_mkdir() - create a directory
 * @dirname: full path to new directory
 *
 * Return: 0 on success, -1 on error
 */
int fat_mkdir(const char *dirname);

/**
 * fat_statfs() - get filesystem statistics
 *
 * @stats: pointer to struct fs_statfs to fill
 * Return: 0 on success, -ve on error
 */
int fat_statfs(struct fs_statfs *stats);

/**
 * fat_close() - close FAT filesystem and release resources
 */
void fat_close(void);

/**
 * fat_next_cluster() - get the next cluster in a chain
 * @itr: directory iterator
 * @nbytes: pointer to store number of bytes in cluster
 *
 * Return: pointer to cluster data buffer
 */
void *fat_next_cluster(struct fat_itr *itr, unsigned int *nbytes);

/**
 * fat_uuid() - get FAT volume ID
 *
 * The FAT volume ID returned in @uuid_str as hexadecimal number in XXXX-XXXX
 * format.
 *
 * @uuid_str:	caller allocated buffer of at least 10 bytes for the volume ID
 * Return:	0 on success
 */
int fat_uuid(char *uuid_str);

#endif /* _FAT_H_ */
