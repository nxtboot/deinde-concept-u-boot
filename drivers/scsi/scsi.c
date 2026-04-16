// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2001
 * Denis Peter, MPL AG Switzerland
 */

#define LOG_CATEGORY	UCLASS_SCSI

#include <blk.h>
#include <bootdev.h>
#include <bootstage.h>
#include <dm.h>
#include <env.h>
#include <libata.h>
#include <log.h>
#include <mapmem.h>
#include <memalign.h>
#include <part.h>
#include <pci.h>
#include <scsi.h>
#include <asm/unaligned.h>
#include <dm/device-internal.h>
#include <dm/uclass-internal.h>

static struct scsi_cmd tempccb;	/* temporary scsi command buffer */

/* temporary data buffer */
#define TEMPBUFF_SIZE	512

DEFINE_CACHE_ALIGN_BUFFER(u8, tempbuff, TEMPBUFF_SIZE);

/* almost the maximum amount of the scsi_ext command.. */
#define SCSI_MAX_BLK 0xFFFF
#define SCSI_LBA48_READ	0xFFFFFFF

#define SCSI_UNMAP_PARAM_RESERVED 0
#define SCSI_UNMAP_PARAM_LEN 22
#define SCSI_UNMAP_PARAM_DATA_LEN 16

static void scsi_print_error(struct scsi_cmd *pccb)
{
	/* Dummy function that could print an error for debugging */
}

static void scsi_setup_cmd(const struct blk_desc *desc, struct scsi_cmd *pccb,
			   uint cmd)
{
	pccb->cmd[0] = cmd;
	pccb->cmd[1] = desc->no_lun ? 0 : pccb->lun << 5;
}

static void scsi_setup_read16(const struct blk_desc *desc,
			      struct scsi_cmd *pccb, lbaint_t start,
			      lbaint_t blocks)
{
	scsi_setup_cmd(desc, pccb, SCSI_READ16);
	put_unaligned_be64(start, &pccb->cmd[2]);
	pccb->cmd[10] = 0;
	pccb->cmd[11] = (unsigned char)(blocks >> 24) & 0xff;
	pccb->cmd[12] = (unsigned char)(blocks >> 16) & 0xff;
	pccb->cmd[13] = (unsigned char)(blocks >> 8) & 0xff;
	pccb->cmd[14] = (unsigned char)blocks & 0xff;
	pccb->cmd[15] = 0;
	pccb->cmdlen = 16;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */
	debug("scsi_setup_read16: cmd: %02X %02X startblk %02X%02X%02X%02X%02X%02X%02X%02X blccnt %02X%02X%02X%02X\n",
	      pccb->cmd[0], pccb->cmd[1],
	      pccb->cmd[2], pccb->cmd[3], pccb->cmd[4], pccb->cmd[5],
	      pccb->cmd[6], pccb->cmd[7], pccb->cmd[8], pccb->cmd[9],
	      pccb->cmd[11], pccb->cmd[12], pccb->cmd[13], pccb->cmd[14]);
}

static void scsi_setup_inquiry(const struct blk_desc *desc,
			       struct scsi_cmd *pccb)
{
	scsi_setup_cmd(desc, pccb, SCSI_INQUIRY);
	pccb->cmd[2] = 0;
	pccb->cmd[3] = 0;
	if (pccb->datalen > 255)
		pccb->cmd[4] = 255;
	else
		pccb->cmd[4] = (unsigned char)pccb->datalen;
	pccb->cmd[5] = 0;
	pccb->cmdlen = 6;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */
}

static void scsi_setup_read_ext(const struct blk_desc *desc,
				struct scsi_cmd *pccb, lbaint_t start,
				lbaint_t blocks)
{
	scsi_setup_cmd(desc, pccb, SCSI_READ10);
	pccb->cmd[2] = (unsigned char)(start >> 24) & 0xff;
	pccb->cmd[3] = (unsigned char)(start >> 16) & 0xff;
	pccb->cmd[4] = (unsigned char)(start >> 8) & 0xff;
	pccb->cmd[5] = (unsigned char)start & 0xff;
	pccb->cmd[6] = 0;
	pccb->cmd[7] = (unsigned char)(blocks >> 8) & 0xff;
	pccb->cmd[8] = (unsigned char)blocks & 0xff;
	pccb->cmd[9] = 0;
	pccb->cmdlen = 10;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */
	debug("scsi_setup_read_ext: cmd: %02X %02X startblk %02X%02X%02X%02X blccnt %02X%02X\n",
	      pccb->cmd[0], pccb->cmd[1],
	      pccb->cmd[2], pccb->cmd[3], pccb->cmd[4], pccb->cmd[5],
	      pccb->cmd[7], pccb->cmd[8]);
}

static void scsi_setup_write_ext(const struct blk_desc *desc,
				 struct scsi_cmd *pccb, lbaint_t start,
				 lbaint_t blocks)
{
	scsi_setup_cmd(desc, pccb, SCSI_WRITE10);
	pccb->cmd[2] = (unsigned char)(start >> 24) & 0xff;
	pccb->cmd[3] = (unsigned char)(start >> 16) & 0xff;
	pccb->cmd[4] = (unsigned char)(start >> 8) & 0xff;
	pccb->cmd[5] = (unsigned char)start & 0xff;
	pccb->cmd[6] = 0;
	pccb->cmd[7] = ((unsigned char)(blocks >> 8)) & 0xff;
	pccb->cmd[8] = (unsigned char)blocks & 0xff;
	pccb->cmd[9] = 0;
	pccb->cmdlen = 10;
	pccb->msgout[0] = SCSI_IDENTIFY;  /* NOT USED */
	debug("%s: cmd: %02X %02X startblk %02X%02X%02X%02X blccnt %02X%02X\n",
	      __func__,
	      pccb->cmd[0], pccb->cmd[1],
	      pccb->cmd[2], pccb->cmd[3], pccb->cmd[4], pccb->cmd[5],
	      pccb->cmd[7], pccb->cmd[8]);
}

static void scsi_setup_erase_ext(struct scsi_cmd *pccb, lbaint_t start,
				 lbaint_t blocks)
{
	u8 *param = tempbuff;
	const u8 param_size = 24;

	memset(param, 0, param_size);
	param[0] = SCSI_UNMAP_PARAM_RESERVED;
	param[1] = SCSI_UNMAP_PARAM_LEN;
	param[2] = SCSI_UNMAP_PARAM_RESERVED;
	param[3] = SCSI_UNMAP_PARAM_DATA_LEN;

	param[8]  = 0x0;
	param[9]  = 0x0;
	param[10] = 0x0;
	param[11] = 0x0;
	param[12] = (start >> 24) & 0xff;
	param[13] = (start >> 16) & 0xff;
	param[14] = (start >> 8) & 0xff;
	param[15] = (start) & 0xff;
	param[16] = (blocks >> 24) & 0xff;
	param[17] = (blocks >> 16) & 0xff;
	param[18] = (blocks >> 8) & 0xff;
	param[19] = (blocks) & 0xff;

	memset(pccb->cmd, 0, sizeof(pccb->cmd));
	pccb->cmd[0] = SCSI_UNMAP;
	pccb->cmd[1] = 0;
	pccb->cmd[6] = 0;
	pccb->cmd[7] = 0;
	pccb->cmd[8] = param_size;
	pccb->cmd[9] = 0;
	pccb->cmdlen = 10;

	pccb->pdata = param;
	pccb->datalen = param_size;
	pccb->dma_dir = DMA_TO_DEVICE;

	debug("%s: cmd: %02X %02X startblk %02X%02X%02X%02X blccnt %02X%02X\n",
	      __func__,
	      pccb->cmd[0], pccb->cmd[1],
	      pccb->cmd[2], pccb->cmd[3], pccb->cmd[4], pccb->cmd[5],
	      pccb->cmd[7], pccb->cmd[8]);
}

static ulong scsi_read(struct udevice *dev, lbaint_t blknr, lbaint_t blkcnt,
		       void *buffer)
{
	struct scsi_cmd *pccb = (struct scsi_cmd *)&tempccb;
	struct blk_desc *desc = dev_get_uclass_plat(dev);
	struct udevice *bdev = dev->parent;
	struct scsi_plat *uc_plat = dev_get_uclass_plat(bdev);
	lbaint_t start, blks, max_blks;
	lbaint_t blocks = 0;
	uintptr_t buf_addr;

	/* Setup device */
	pccb->target = desc->target;
	pccb->lun = desc->lun;
	if (uc_plat->max_bytes_per_req)
		max_blks = uc_plat->max_bytes_per_req / desc->blksz;
	else
		max_blks = SCSI_MAX_BLK;

	start = blknr;	/* start block# for each read */
	blks = blkcnt;	/* number of blocks remaining to read */
	buf_addr = map_to_sysmem(buffer);
	debug("\nscsi_read: dev %d startblk " LBAF
	      ", blccnt " LBAF " buffer %lx\n",
	      desc->devnum, start, blks, (unsigned long)buffer);
	do {
		ulong to_read;	/* number of blocks to read on this iteration */

		to_read = blks;
		pccb->pdata = map_sysmem(buf_addr, 0);
		pccb->dma_dir = DMA_FROM_DEVICE;
		if (IS_ENABLED(CONFIG_SYS_64BIT_LBA) &&
		    start > SCSI_LBA48_READ) {
			to_read = min_t(lbaint_t, to_read, max_blks);
			pccb->datalen = desc->blksz * to_read;
			scsi_setup_read16(desc, pccb, start, to_read);
		} else if (to_read > max_blks) {
			to_read = max_blks;
			pccb->datalen = desc->blksz * to_read;
			blocks = to_read;
			scsi_setup_read_ext(desc, pccb, start, blocks);
		} else {
			pccb->datalen = desc->blksz * to_read;
			blocks = to_read;
			scsi_setup_read_ext(desc, pccb, start, blocks);
		}
		debug("scsi_read_ext: startblk " LBAF
		      ", blccnt " LBAF " buffer %lX\n",
		      start, blocks, buf_addr);
		if (scsi_exec(bdev, pccb)) {
			scsi_print_error(pccb);
			break;
		}

		/* update ready for the next read */
		start += to_read;
		blks -= to_read;
		buf_addr += pccb->datalen;
	} while (blks);
	debug("scsi_read_ext: end startblk " LBAF
	      ", blccnt " LBAF " buffer %lX\n", start, blocks, buf_addr);

	/* Report an I/O error if nothing was read */
	if (blks == blkcnt)
		return -EIO;

	return blkcnt - blks;
}

/*******************************************************************************
 * scsi_write
 */

static ulong scsi_write(struct udevice *dev, lbaint_t blknr, lbaint_t blkcnt,
			const void *buffer)
{
	struct scsi_cmd *pccb = (struct scsi_cmd *)&tempccb;
	struct blk_desc *desc = dev_get_uclass_plat(dev);
	struct udevice *bdev = dev->parent;
	struct scsi_plat *uc_plat = dev_get_uclass_plat(bdev);
	lbaint_t start, blks, max_blks;
	lbaint_t blocks;
	uintptr_t buf_addr;

	/* Setup device */
	pccb->target = desc->target;
	pccb->lun = desc->lun;
	if (uc_plat->max_bytes_per_req)
		max_blks = uc_plat->max_bytes_per_req / desc->blksz;
	else
		max_blks = SCSI_MAX_BLK;

	start = blknr;
	blks = blkcnt;
	buf_addr = map_to_sysmem(buffer);
	debug("\n%s: dev %d startblk " LBAF ", blccnt " LBAF " buffer %lx\n",
	      __func__, desc->devnum, start, blks, (unsigned long)buffer);
	do {
		ulong to_write;	 /* # blocks to write on this iteration */

		to_write = blks;
		pccb->pdata = map_sysmem(buf_addr, 0);
		pccb->dma_dir = DMA_TO_DEVICE;
		if (to_write > max_blks) {
			to_write = max_blks;
			pccb->datalen = desc->blksz * to_write;
			blocks = to_write;
			scsi_setup_write_ext(desc, pccb, start, to_write);
		} else {
			pccb->datalen = desc->blksz * to_write;
			blocks = to_write;
			scsi_setup_write_ext(desc, pccb, start, blocks);
		}
		debug("%s: startblk " LBAF ", blccnt " LBAF " buffer %lx\n",
		      __func__, start, blocks, buf_addr);
		if (scsi_exec(bdev, pccb)) {
			scsi_print_error(pccb);
			break;
		}

		/* update ready for the next write */
		buf_addr += pccb->datalen;
		start += to_write;
		blks -= to_write;
	} while (blks);

	debug("%s: end startblk " LBAF ", blccnt " LBAF " buffer %lX\n",
	      __func__, start, blocks, buf_addr);

	/* Report an I/O error if nothing was written */
	if (blks == blkcnt)
		return -EIO;

	return blkcnt - blks;
}

/*******************************************************************************
 * scsi_erase
 */
static ulong scsi_erase(struct udevice *dev, lbaint_t blknr, lbaint_t blkcnt)
{
	struct blk_desc *block_dev = dev_get_uclass_plat(dev);
	struct udevice *bdev = dev->parent;
	struct scsi_plat *uc_plat = dev_get_uclass_plat(bdev);
	lbaint_t start, blks, max_blks;
	struct scsi_cmd *pccb = (struct scsi_cmd *)&tempccb;

	/* Setup device */
	pccb->target = block_dev->target;
	pccb->lun = block_dev->lun;
	start = blknr;
	blks = blkcnt;
	if (uc_plat->max_bytes_per_req)
		max_blks = uc_plat->max_bytes_per_req / block_dev->blksz;
	else
		max_blks = SCSI_MAX_BLK;

	debug("\n%s: dev %d startblk " LBAF ", blccnt " LBAF "\n",
	      __func__, block_dev->devnum, start, blks);
	do {
		if (blks > max_blks) {
			scsi_setup_erase_ext(pccb, start, max_blks);
			start += max_blks;
			blks -= max_blks;
		} else {
			scsi_setup_erase_ext(pccb, start, blks);
			start += blks;
			blks = 0;
		}
		if (scsi_exec(bdev, pccb)) {
			scsi_print_error(pccb);
			blkcnt -= blks;
			break;
		}
	} while (blks != 0);
	return blkcnt;
}

#if IS_ENABLED(CONFIG_BOUNCE_BUFFER)
static int scsi_buffer_aligned(struct udevice *dev, struct bounce_buffer *state)
{
	struct scsi_ops *ops = scsi_get_ops(dev->parent);

	if (ops->buffer_aligned)
		return ops->buffer_aligned(dev->parent, state);

	return 1;
}
#endif	/* CONFIG_BOUNCE_BUFFER */

/* copy src to dest, skipping leading and trailing blanks
 * and null terminate the string
 */
static void scsi_ident_cpy(char *dest, const char *src, uint len)
{
	int start, end;

	start = 0;
	while (start < len) {
		if (src[start] != ' ')
			break;
		start++;
	}
	end = len-1;
	while (end > start) {
		if (src[end] != ' ')
			break;
		end--;
	}
	for (; start <= end; start++)
		*dest ++= src[start];
	*dest = '\0';
}

static int scsi_read_capacity(struct udevice *dev, struct scsi_cmd *pccb,
			      lbaint_t *capacity, unsigned long *blksz)
{
	const struct blk_desc *desc = dev_get_uclass_plat(dev);

	*capacity = 0;

	memset(pccb->cmd, '\0', sizeof(pccb->cmd));
	scsi_setup_cmd(desc, pccb, SCSI_RD_CAPAC10);
	pccb->cmdlen = 10;
	pccb->dma_dir = DMA_FROM_DEVICE;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */

	pccb->datalen = 8;
	if (scsi_exec(dev, pccb))
		return 1;

	*capacity = ((lbaint_t)pccb->pdata[0] << 24) |
		    ((lbaint_t)pccb->pdata[1] << 16) |
		    ((lbaint_t)pccb->pdata[2] << 8)  |
		    ((lbaint_t)pccb->pdata[3]);

	if (*capacity != 0xffffffff) {
		/* Read capacity (10) was sufficient for this drive. */
		*blksz = ((unsigned long)pccb->pdata[4] << 24) |
			 ((unsigned long)pccb->pdata[5] << 16) |
			 ((unsigned long)pccb->pdata[6] << 8)  |
			 ((unsigned long)pccb->pdata[7]);
		*capacity += 1;
		return 0;
	}

	/* Read capacity (10) was insufficient. Use read capacity (16). */
	memset(pccb->cmd, '\0', sizeof(pccb->cmd));
	pccb->cmd[0] = SCSI_RD_CAPAC16;
	pccb->cmd[1] = 0x10;
	pccb->cmdlen = 16;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */

	pccb->datalen = 16;
	pccb->dma_dir = DMA_FROM_DEVICE;
	if (scsi_exec(dev, pccb))
		return 1;

	*capacity = ((uint64_t)pccb->pdata[0] << 56) |
		    ((uint64_t)pccb->pdata[1] << 48) |
		    ((uint64_t)pccb->pdata[2] << 40) |
		    ((uint64_t)pccb->pdata[3] << 32) |
		    ((uint64_t)pccb->pdata[4] << 24) |
		    ((uint64_t)pccb->pdata[5] << 16) |
		    ((uint64_t)pccb->pdata[6] << 8)  |
		    ((uint64_t)pccb->pdata[7]);
	*capacity += 1;

	*blksz = ((uint64_t)pccb->pdata[8]  << 56) |
		 ((uint64_t)pccb->pdata[9]  << 48) |
		 ((uint64_t)pccb->pdata[10] << 40) |
		 ((uint64_t)pccb->pdata[11] << 32) |
		 ((uint64_t)pccb->pdata[12] << 24) |
		 ((uint64_t)pccb->pdata[13] << 16) |
		 ((uint64_t)pccb->pdata[14] << 8)  |
		 ((uint64_t)pccb->pdata[15]);

	return 0;
}

/*
 * Some setup (fill-in) routines
 */
static void scsi_setup_test_unit_ready(const struct blk_desc *desc,
				       struct scsi_cmd *pccb)
{
	scsi_setup_cmd(desc, pccb, SCSI_TST_U_RDY);
	pccb->cmd[2] = 0;
	pccb->cmd[3] = 0;
	pccb->cmd[4] = 0;
	pccb->cmd[5] = 0;
	pccb->cmdlen = 6;
	pccb->msgout[0] = SCSI_IDENTIFY; /* NOT USED */
}

/**
 * scsi_init_desc_priv - initialize only SCSI specific blk_desc properties
 *
 * @desc: Block device description pointer
 */
static void scsi_init_desc_priv(struct blk_desc *desc)
{
	memset(desc, 0, sizeof(struct blk_desc));
	desc->target = 0xff;
	desc->lun = 0xff;
	desc->log2blksz =
		LOG2_INVALID(typeof(desc->log2blksz));
	desc->type = DEV_TYPE_UNKNOWN;
#if IS_ENABLED(CONFIG_BOUNCE_BUFFER)
	desc->bb = true;
#endif	/* CONFIG_BOUNCE_BUFFER */
}

/**
 * scsi_count_luns() - Count the number of LUNs on a given target
 *
 * @dev: SCSI device
 * @target: Target to look up
 * Return: Number of LUNs on this target, or 0 if None, or -ve on error
 */
static int scsi_count_luns(struct udevice *dev, uint target)
{
	struct scsi_cmd *pccb = (struct scsi_cmd *)&tempccb;
	uint rec_count;
	int ret, i, max_lun;
	u8 *luns;

	memset(pccb->cmd, '\0', sizeof(pccb->cmd));
	pccb->cmd[0] = SCSI_REPORT_LUNS;
	pccb->target = target;
	pccb->lun = 0;

	/* Select Report: 0x00 for all LUNs */
	pccb->cmd[2] = 0;
	put_unaligned_be32(TEMPBUFF_SIZE, &pccb->cmd[6]);

	pccb->cmdlen = 12;
	pccb->pdata = tempbuff;
	pccb->datalen = TEMPBUFF_SIZE;
	pccb->dma_dir = DMA_FROM_DEVICE;

	ret = scsi_exec(dev, pccb);
	if (ret == -ENODEV) {
		/* target doesn't exist, return 0 */
		return 0;
	}
	if (ret) {
		scsi_print_error(pccb);
		return -EINVAL;
	}

	/* find the maximum LUN */
	rec_count = get_unaligned_be32(tempbuff) / 8;
	luns = &tempbuff[8];

	max_lun = -1;
	for (i = 0; i < rec_count; i++, luns += 8) {
		int lun = get_unaligned_be16(luns) & 0x3fff;

		max_lun = max(max_lun, lun);
	}

	return max_lun + 1;
}

/**
 * scsi_detect_dev - Detect scsi device
 *
 * @target: target id
 * @lun: target lun
 * @desc: block device description
 *
 * The scsi_detect_dev detects and fills a desc structure when the device is
 * detected.
 *
 * Return: 0 on success, error value otherwise
 */
static int scsi_detect_dev(struct udevice *dev, int target, int lun,
			   struct blk_desc *desc)
{
	lbaint_t capacity;
	unsigned long blksz;
	struct scsi_cmd *pccb = (struct scsi_cmd *)&tempccb;
	const struct scsi_inquiry_resp *resp;
	int count, err;

	pccb->target = target;
	pccb->lun = lun;
	pccb->pdata = tempbuff;
	pccb->datalen = 36;
	pccb->dma_dir = DMA_FROM_DEVICE;
	scsi_setup_inquiry(desc, pccb);
	if (scsi_exec(dev, pccb)) {
		if (pccb->contr_stat == SCSI_SEL_TIME_OUT) {
			/*
			  * selection timeout => assuming no
			  * device present
			  */
			debug("Selection timeout ID %d\n",
			      pccb->target);
			return -ETIMEDOUT;
		}
		scsi_print_error(pccb);
		return -ENODEV;
	}
	resp = (const struct scsi_inquiry_resp *)tempbuff;
	if ((resp->type & SCSIRF_TYPE_MASK) == SCSIRF_TYPE_UNKNOWN)
		return -ENODEV; /* skip unknown devices */
	if (resp->flags & SCSIRF_FLAGS_REMOVABLE) /* drive is removable */
		desc->removable = true;
	if (resp->eflags & EFLAGS_TPGS_MASK)
		desc->no_lun = true;
	/* check for SPC-3 or greater */
	if (resp->version > 4)
		desc->no_lun = true;

	/* get info for this device */
	scsi_ident_cpy(desc->vendor, resp->vendor, sizeof(resp->vendor));
	scsi_ident_cpy(desc->product, resp->product, sizeof(resp->product));
	scsi_ident_cpy(desc->revision, resp->revision,
		       sizeof(resp->revision));
	desc->target = pccb->target;
	desc->lun = pccb->lun;
	desc->type = resp->type;

	/* this is about to be overwritten by the code below */
	resp = NULL;

	for (count = 0; count < 3; count++) {
		pccb->datalen = 0;
		pccb->dma_dir = DMA_NONE;
		scsi_setup_test_unit_ready(desc, pccb);
		err = scsi_exec(dev, pccb);
		if (!err)
			break;
	}
	if (err) {
		if (desc->removable)
			goto removable;
		scsi_print_error(pccb);
		return -EINVAL;
	}
	if (scsi_read_capacity(dev, pccb, &capacity, &blksz)) {
		scsi_print_error(pccb);
		return -EINVAL;
	}
	desc->lba = capacity;
	desc->blksz = blksz;
	desc->log2blksz = LOG2(desc->blksz);

removable:
	return 0;
}

/*
 * (re)-scan the scsi bus and reports scsi device info
 * to the user if mode = 1
 */
static int do_scsi_scan_one(struct udevice *dev, int id, int lun, bool verbose)
{
	int ret;
	struct udevice *bdev;
	struct blk_desc bd;
	struct blk_desc *bdesc;
	char str[10];

	/*
	 * detect the scsi driver to get information about its geometry (block
	 * size, number of blocks) and other parameters (ids, type, ...)
	 */
	scsi_init_desc_priv(&bd);
	if (scsi_detect_dev(dev, id, lun, &bd))
		return -ENODEV;

	/*
	* Create only one block device and do detection
	* to make sure that there won't be a lot of
	* block devices created
	*/
	snprintf(str, sizeof(str), "id%xlun%x", id, lun);
	ret = blk_create_devicef(dev, "scsi_blk", str, UCLASS_SCSI, -1,
				 bd.blksz, bd.lba, &bdev);
	if (ret) {
		debug("Can't create device\n");
		return ret;
	}
	device_set_name_alloced(bdev);

	bdesc = dev_get_uclass_plat(bdev);
	bdesc->target = id;
	bdesc->lun = lun;
	bdesc->removable = bd.removable;
	bdesc->type = bd.type;
	bdesc->bb = bd.bb;
	bdesc->no_lun = bd.no_lun;
	memcpy(&bdesc->vendor, &bd.vendor, sizeof(bd.vendor));
	memcpy(&bdesc->product, &bd.product, sizeof(bd.product));
	memcpy(&bdesc->revision, &bd.revision,	sizeof(bd.revision));
	if (IS_ENABLED(CONFIG_SYS_BIG_ENDIAN)) {
		ata_swap_buf_le16((u16 *)&bdesc->vendor, sizeof(bd.vendor) / 2);
		ata_swap_buf_le16((u16 *)&bdesc->product, sizeof(bd.product) / 2);
		ata_swap_buf_le16((u16 *)&bdesc->revision, sizeof(bd.revision) / 2);
	}

	ret = blk_probe_or_unbind(bdev);
	if (ret < 0)
		/* TODO: undo create */
		return log_msg_ret("pro", ret);

	ret = bootdev_setup_for_sibling_blk(bdev, "scsi_bootdev");
	if (ret)
		return log_msg_ret("bd", ret);

	if (verbose) {
		log_debug("id %x lun %x:\n", id, lun);
		printf("  Device %d: ", bdesc->devnum);
		dev_print(bdesc);
	}
	return 0;
}

int scsi_scan_dev(struct udevice *dev, bool verbose)
{
	struct scsi_plat *uc_plat; /* scsi controller plat */
	int ret, i;

	/* probe SCSI controller driver */
	ret = device_probe(dev);
	if (ret)
		return ret;

	/* Get controller plat */
	uc_plat = dev_get_uclass_plat(dev);

	log_debug("max_id %lx max_lun %lx\n", uc_plat->max_id,
		  uc_plat->max_lun);

	for (i = 0; i <= uc_plat->max_id; i++) {
		uint lun_count, lun;

		/* try to count the number of LUNs */
		ret = scsi_count_luns(dev, i);
		if (ret < 0)
			lun_count = uc_plat->max_lun + 1;
		else
			lun_count = ret;
		if (!lun_count)
			continue;
		log_debug("Target %x: scanning up to LUN %x\n", i,
			  lun_count - 1);
		for (lun = 0; lun < lun_count; lun++)
			do_scsi_scan_one(dev, i, lun, verbose);
	}

	return 0;
}

int scsi_scan(bool verbose)
{
	struct uclass *uc;
	struct udevice *dev; /* SCSI controller */
	int ret;

	if (verbose)
		printf("scanning bus for devices...\n");

	ret = uclass_get(UCLASS_SCSI, &uc);
	if (ret)
		return ret;

	/* remove all children of the SCSI devices */
	uclass_foreach_dev(dev, uc) {
		log_debug("unbind %s\n", dev->name);
		ret = device_chld_remove(dev, NULL, DM_REMOVE_NORMAL);
		if (!ret)
			ret = device_chld_unbind(dev, NULL);
		if (ret) {
			if (verbose)
				printf("unable to unbind devices (%dE)\n", ret);
			return log_msg_ret("unb", ret);
		}
	}

	uclass_foreach_dev(dev, uc) {
		ret = scsi_scan_dev(dev, verbose);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct blk_ops scsi_blk_ops = {
	.read	= scsi_read,
	.write	= scsi_write,
	.erase  = scsi_erase,
#if IS_ENABLED(CONFIG_BOUNCE_BUFFER)
	.buffer_aligned	= scsi_buffer_aligned,
#endif	/* CONFIG_BOUNCE_BUFFER */
};

U_BOOT_DRIVER(scsi_blk) = {
	.name		= "scsi_blk",
	.id		= UCLASS_BLK,
	.ops		= &scsi_blk_ops,
};
