// SPDX-License-Identifier: GPL-2.0+
/*
 * VirtIO Sandbox emulator, for testing purpose only. This emulates the QEMU
 * side of virtio, using the MMIO driver and handling any accesses
 *
 * This handles traffic from the virtio_ring
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	UCLASS_VIRTIO

#include <dm.h>
#include <malloc.h>
#include <virtio.h>
#include <virtio_mmio.h>
#include <asm/io.h>
#include <dt-bindings/virtio.h>
#include <asm/state.h>
#include <linux/sizes.h>
#include "sandbox_emul.h"
#include "virtio_types.h"
#include "virtio_blk.h"
#include "virtio_internal.h"
#include "virtio_mmio.h"
#include "virtio_ring.h"

enum {
	MMIO_SIZE		= 0x200,
	VENDOR_ID		= 0xf003,
	DEVICE_ID		= VIRTIO_ID_BLOCK,
	DISK_SIZE_MB		= 16,
};

void process_queue(struct udevice *emul_dev, struct sandbox_emul_priv *priv,
		   uint32_t queue_idx)
{
	struct virtio_emul_ops *ops = virtio_emul_get_ops(emul_dev);
	bool processed_something = false;
	struct virtio_emul_queue *q;
	struct vring_avail *avail;
	struct vring_desc *desc;
	struct vring_used *used;
	uint old_used_idx;

	if (queue_idx >= priv->num_queues)
		return;
	log_debug("Notified on queue %u\n", queue_idx);

	q = &priv->queues[queue_idx];
	if (!q->ready)
		return;

	desc = (struct vring_desc *)q->desc_addr;
	avail = (struct vring_avail *)q->avail_addr;
	used = (struct vring_used *)q->used_addr;
	old_used_idx = used->idx;

	while (q->last_avail_idx != avail->idx) {
		processed_something = true;
		uint ring_idx = q->last_avail_idx % q->num;
		uint desc_head_idx = avail->ring[ring_idx];
		uint used_ring_idx;
		int written;
		int ret;

		log_debug("Found request at avail ring index %u (desc head %u)\n",
			  ring_idx, desc_head_idx);

		ret = ops->process_request(emul_dev, desc, desc_head_idx,
					   &written);
		if (ret)
			log_warning("Failed to process request (err=%dE)\n",
				    ret);

		used_ring_idx = used->idx % q->num;
		used->ring[used_ring_idx].id = desc_head_idx;
		used->ring[used_ring_idx].len = written;
		used->idx++;
		q->last_avail_idx++;
	}

	if (processed_something) {
		bool needs_interrupt = true;

		log_debug("finished processing, new used_idx is %d.\n",
			  used->idx);
		if (priv->driver_features & BIT(VIRTIO_RING_F_EVENT_IDX)) {
			struct {
				struct vring_avail *avail;
				unsigned int num;
			} vr;

			vr.avail = avail;
			vr.num = q->num;

			needs_interrupt =
				 vring_need_event(vring_used_event((&vr)),
						  used->idx, old_used_idx);
			log_debug("EVENT_IDX is enabled; driver wants event "
				  "at %u needs_interrupt %d\n",
				  vring_used_event(&vr), needs_interrupt);
		}

		if (needs_interrupt) {
			log_debug("sending VRING interrupt\n");
			priv->interrupt_status |= VIRTIO_MMIO_INT_VRING;
		}
	}
}

long h_read(void *ctx, const void *addr, enum sandboxio_size_t size)
{
	struct udevice *dev = ctx;
	struct udevice *emul_dev = dev_get_parent(dev);
	struct sandbox_emul_priv *priv = dev_get_priv(dev);
	ulong offset = (ulong)addr - (ulong)priv->mmio.base;
	struct virtio_emul_ops *ops = virtio_emul_get_ops(emul_dev);
	struct virtio_emul_queue *q;
	u32 val = 0;

	if (offset >= VIRTIO_MMIO_CONFIG) {
		ulong config_offset = offset - VIRTIO_MMIO_CONFIG;
		int ret;

		ret = ops->get_config(emul_dev, config_offset, &val, size);
		if (ret)
			log_warning("Failed to process request (err=%dE)\n",
				    ret);
		return val;
	}

	if (priv->queue_sel >= priv->num_queues) {
		log_debug("invalid queue_sel %d\n", priv->queue_sel);
		return 0;
	}
	q = &priv->queues[priv->queue_sel];

	switch (offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		return ('v' | 'i' << 8 | 'r' << 16 | 't' << 24);
	case VIRTIO_MMIO_VERSION:
		return 2;
	case VIRTIO_MMIO_DEVICE_ID:
		return ops->get_device_id(emul_dev);
	case VIRTIO_MMIO_VENDOR_ID:
		return VENDOR_ID;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		return !priv->features_sel ?
			(priv->features & 0xffffffff) :
			(priv->features >> 32);
	case VIRTIO_MMIO_QUEUE_NUM_MAX:
		return QUEUE_MAX_SIZE;
	case VIRTIO_MMIO_QUEUE_READY:
		return q->ready;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		return priv->interrupt_status;
	case VIRTIO_MMIO_STATUS:
		return priv->status;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		return q->desc_addr & 0xffffffff;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		return q->desc_addr >> 32;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		return q->avail_addr & 0xffffffff;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		return q->avail_addr >> 32;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		return q->used_addr & 0xffffffff;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		return q->used_addr >> 32;
	case VIRTIO_MMIO_CONFIG_GENERATION:
		return priv->config_generation;
	default:
		log_debug("unhandled read from offset 0x%lx\n", offset);
		return 0;
	}
}

void h_write(void *ctx, void *addr, unsigned int val,
	     enum sandboxio_size_t size)
{
	struct udevice *dev = ctx;
	struct udevice *emul_dev = dev_get_parent(dev);
	struct sandbox_emul_priv *priv = dev_get_priv(dev);
	ulong offset = (ulong)addr - (ulong)priv->mmio.base;
	struct virtio_emul_queue *q;

	if (offset >= VIRTIO_MMIO_CONFIG)
		return;

	if (priv->queue_sel >= priv->num_queues && offset != VIRTIO_MMIO_QUEUE_SEL)
		return;
	q = &priv->queues[priv->queue_sel];

	switch (offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		priv->features_sel = val;
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		if (priv->features_sel == 0)
			priv->driver_features = (priv->driver_features &
						 0xffffffff00000000) | val;
		else
			priv->driver_features = (priv->driver_features &
						 0xffffffff) | ((u64)val << 32);
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		priv->features_sel = val;
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		if (val < priv->num_queues)
			priv->queue_sel = val;
		else
			log_debug("tried to select invalid queue %u\n", val);
		break;
	case VIRTIO_MMIO_QUEUE_NUM:
		q->num = (val > 0 && val <= QUEUE_MAX_SIZE) ? val : 0;
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		q->ready = val & 0x1;
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		process_queue(emul_dev, priv, val);
		break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		priv->interrupt_status &= ~val;
		break;
	case VIRTIO_MMIO_STATUS:
		priv->status = val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		q->desc_addr = (q->desc_addr & 0xffffffff00000000) | val;
		break;
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		q->desc_addr = (q->desc_addr & 0xffffffff) | ((u64)val << 32);
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		q->avail_addr = (q->avail_addr & 0xffffffff00000000) | val;
		break;
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		q->avail_addr = (q->avail_addr & 0xffffffff) | ((u64)val << 32);
		break;
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		q->used_addr = (q->used_addr & 0xffffffff00000000) | val;
		break;
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		q->used_addr = (q->used_addr & 0xffffffff) | ((u64)val << 32);
		break;
	default:
		log_debug("unhandled write to offset 0x%lx\n", offset);
		break;
	}
}

static int sandbox_emul_of_to_plat(struct udevice *dev)
{
	struct udevice *emul_dev = dev_get_parent(dev);
	struct virtio_emul_ops *ops = virtio_emul_get_ops(emul_dev);
	struct sandbox_emul_priv *priv = dev_get_priv(dev);
	struct virtio_mmio_plat *plat = dev_get_plat(dev);
	int ret;

	/* set up the MMIO base so that virtio_mmio_probe() can find it */
	priv->mmio.base = memalign(SZ_4K, MMIO_SIZE);
	if (!priv->mmio.base)
		return -ENOMEM;
	plat->base = (phys_addr_t)(uintptr_t)priv->mmio.base;

	ret = sandbox_mmio_add(priv->mmio.base, MMIO_SIZE, h_read, h_write,
			       dev);
	if (ret) {
		free(priv->mmio.base);
		return log_msg_ret("sep", ret);
	}

	priv->num_queues = MAX_VIRTIO_QUEUES;
	priv->features = BIT(VIRTIO_F_VERSION_1) |
				BIT(VIRTIO_RING_F_EVENT_IDX) |
				ops->get_features(emul_dev);

	log_debug("sandbox virtio emulator, mmio %p\n", priv->mmio.base);

	return 0;
}

static int sandbox_emul_remove(struct udevice *dev)
{
	struct sandbox_emul_priv *priv = dev_get_priv(dev);

	sandbox_mmio_remove(dev);
	free(priv->mmio.base);

	return 0;
}

static const struct udevice_id virtio_sandbox2_ids[] = {
	{ .compatible = "sandbox,virtio-emul" },
	{ }
};

U_BOOT_DRIVER(virtio_emul) = {
	.name	= "virtio-emul",
	.id	= UCLASS_VIRTIO,
	.of_match = virtio_sandbox2_ids,
	.probe	= virtio_mmio_probe,
	.remove	= sandbox_emul_remove,
	.ops	= &virtio_mmio_ops,
	.of_to_plat	= sandbox_emul_of_to_plat,
	.priv_auto	= sizeof(struct sandbox_emul_priv),
	.plat_auto	= sizeof(struct virtio_mmio_plat),
};

UCLASS_DRIVER(virtio_emul) = {
	.name	= "virtio_emul",
	.id	= UCLASS_VIRTIO_EMUL,
#if CONFIG_IS_ENABLED(OF_REAL)
	.post_bind	= dm_scan_fdt_dev,
#endif
};
