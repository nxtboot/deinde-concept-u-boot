// SPDX-License-Identifier: GPL-2.0+
/*
 * Emulation of a block device. This implements a simple version of the QEMU
 * side of the interface.
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_VIRTIO

#include <dm.h>
#include <malloc.h>
#include <asm/io.h>
#include <dt-bindings/virtio.h>
#include <linux/sizes.h>
#include "virtio_blk.h"
#include "virtio_ring.h"
#include "sandbox_emul.h"

enum {
	DISK_SIZE_MB	= 1,
	SECTOR_SIZE	= 512,
};

/**
 * struct virtio_blk_emul_priv - private data for the block emulator
 *
 * @config: virtio block-device-configuration structure, exposed to the driver
 *	through the config space
 * @disk_data: allocated memory for the virtual disk
 * @disk_size: total size of the virtual disk in bytes
 */
struct virtio_blk_emul_priv {
	struct virtio_blk_config config;
	void *disk_data;
	u64 disk_size;
};

static int blk_emul_process_request(struct udevice *dev,
				    struct vring_desc *descs, u32 head_idx,
				    int *writtenp)
{
	struct virtio_blk_emul_priv *priv = dev_get_priv(dev);
	struct vring_desc *hdr_desc, *data_desc, *status_desc;
	struct virtio_blk_outhdr *hdr;
	void *data_buf;
	u64 offset;
	u8 *status;

	hdr_desc = &descs[head_idx];
	if (!(hdr_desc->flags & VRING_DESC_F_NEXT))
		return -EIO;
	data_desc = &descs[hdr_desc->next];
	if (!(data_desc->flags & VRING_DESC_F_NEXT))
		return -EIO;
	status_desc = &descs[data_desc->next];

	hdr = (struct virtio_blk_outhdr *)hdr_desc->addr;
	status = (u8 *)status_desc->addr;

	offset = hdr->sector * SECTOR_SIZE;
	if (offset + data_desc->len > priv->disk_size) {
		*status = VIRTIO_BLK_S_IOERR;
		*writtenp = 1;
		return 0;
	}

	data_buf = (void *)data_desc->addr;

	switch (hdr->type) {
	case VIRTIO_BLK_T_IN:
		log_debug("read: sector %lld, len %u\n", hdr->sector,
			  data_desc->len);
		memcpy(data_buf, priv->disk_data + offset, data_desc->len);
		*writtenp = data_desc->len;
		break;
	case VIRTIO_BLK_T_OUT:
		log_debug("write: sector %lld, len %u\n", hdr->sector,
			  data_desc->len);
		memcpy(priv->disk_data + offset, data_buf, data_desc->len);
		*writtenp = 0;
		break;
	default:
		log_warning("unknown request type 0x%x\n", hdr->type);
		*status = VIRTIO_BLK_S_UNSUPP;
		*writtenp = 1;
		return 0;
	}

	*status = VIRTIO_BLK_S_OK;
	*writtenp += 1; /* For the status byte */

	return 0;
}

static int blk_emul_get_config(struct udevice *dev, ulong offset, void *buf,
			       enum sandboxio_size_t size)
{
	struct virtio_blk_emul_priv *priv = dev_get_priv(dev);

	if (offset + size > sizeof(priv->config))
		return -EIO;

	memcpy(buf, (u8 *)&priv->config + offset, size);

	return 0;
}

static u64 blk_emul_get_features(struct udevice *dev)
{
	return BIT(VIRTIO_BLK_F_BLK_SIZE);
}

static u32 blk_emul_get_device_id(struct udevice *dev)
{
	return VIRTIO_ID_BLOCK;
}

static int virtio_blk_emul_probe(struct udevice *dev)
{
	struct virtio_blk_emul_priv *priv = dev_get_priv(dev);

	priv->disk_size = (u64)DISK_SIZE_MB * SZ_1M;
	priv->disk_data = calloc(1, priv->disk_size);
	if (!priv->disk_data)
		return -ENOMEM;

	priv->config.capacity = priv->disk_size / SECTOR_SIZE;
	priv->config.blk_size = SECTOR_SIZE;

	return 0;
}

static int virtio_blk_emul_remove(struct udevice *dev)
{
	struct virtio_blk_emul_priv *priv = dev_get_priv(dev);

	free(priv->disk_data);

	return 0;
}

static struct virtio_emul_ops blk_emul_ops = {
	.process_request = blk_emul_process_request,
	.get_config = blk_emul_get_config,
	.get_features = blk_emul_get_features,
	.get_device_id = blk_emul_get_device_id,
};

static const struct udevice_id virtio_blk_emul_ids[] = {
	{ .compatible = "sandbox,virtio-blk-emul" },
	{ }
};

U_BOOT_DRIVER(virtio_blk_emul) = {
	.name	= "virtio_blk_emul",
	.id	= UCLASS_VIRTIO_EMUL,
	.of_match = virtio_blk_emul_ids,
	.probe	= virtio_blk_emul_probe,
	.remove	= virtio_blk_emul_remove,
	.ops	= &blk_emul_ops,
	.priv_auto	= sizeof(struct virtio_blk_emul_priv),
};
