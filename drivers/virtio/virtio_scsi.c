// SPDX-License-Identifier: GPL-2.0+
/*
 * U-Boot driver for VirtIO SCSI host devices
 *
 * This driver implements the host-side interface for VirtIO SCSI devices,
 * allowing U-Boot to communicate with SCSI LUNs provided by a hypervisor.
 *
 * Based on the VirtIO v1.3 specification, Section 5.6: SCSI Host Device
 */

#define LOG_CATEGORY	UCLASS_VIRTIO

#include <blk.h>
#include <dm.h>
#include <malloc.h>
#include <scsi.h>
#include <virtio.h>
#include <virtio_ring.h>
#include "virtio_scsi.h"

static_assert(CMD_BUF_SIZE >= VIRTIO_SCSI_CDB_SIZE);
static_assert(SENSE_BUF_LEN >= VIRTIO_SCSI_SENSE_SIZE);

/*
 * We need three virtqueues: controlq, eventq, and requestq.
 * The request queue is the one we will use for actual SCSI commands.
 */
enum {
	QUEUE_CONTROL,
	QUEUE_EVENT,
	QUEUE_REQUEST,

	QUEUE_COUNT,
};

/*
 * struct virtio_scsi_cmd - represents a pending SCSI command
 *
 * The address of this struct is used as a 'cookie' to identify the request in
 * the virtqueue, although in practice there is only one pending request at a
 * time
 */
struct virtio_scsi_cmd {
	struct virtio_scsi_cmd_req req;
	struct virtio_scsi_cmd_resp resp;
};

/**
 * struct virtio_scsi_priv - Per-device private data
 *
 * @vqs:	Array of virtqueues for this device
 * @max_target:	Maximum target ID supported by the device
 * @max_lun:	Maximum LUN ID supported by the device
 * @v_cmd:	Pre-allocated virtio-command struct to avoid allocation in the
 *		I/O path
 * @id:         ID number for the next transation
 */
struct virtio_scsi_priv {
	struct virtqueue *vqs[QUEUE_COUNT];
	int max_target;
	int max_lun;
	int id;
	/* Pre-allocated command struct to avoid allocation in send_cmd */
	struct virtio_scsi_cmd v_cmd;
};

u32 features[] = {
	VIRTIO_SCSI_F_INOUT,
};

static int virtio_scsi_exec(struct udevice *dev, struct scsi_cmd *cmd)
{
	struct virtio_scsi_priv *priv = dev_get_priv(dev);
	struct virtqueue *vq = priv->vqs[QUEUE_REQUEST];
	struct virtio_scsi_cmd *v_cmd = &priv->v_cmd;
	struct virtio_sg out_sgs[2], in_sgs[2];
	struct virtio_sg *sgs[4];
	uint out_count = 0, in_count = 0;
	int len, i, ret;
	uint dummy_len;

	/* Clear the pre-allocated command struct to avoid stale data */
	memset(v_cmd, '\0', sizeof(*v_cmd));

	/*
	 * Fill in the request header.
	 * The LUN is encoded in a specific way for virtio-scsi.
	 * LUN 0 is encoded as 0x4000. LUNs 1-255 are encoded as themselves.
	 */
	v_cmd->req.lun[0] = 1;
	v_cmd->req.lun[1] = cmd->target;
	v_cmd->req.lun[2] = 0x40 | ((cmd->lun >> 8) & 0x3f);
	v_cmd->req.lun[3] = cmd->lun & 0xff;
	v_cmd->req.tag = priv->id++;
	v_cmd->req.task_attr = VIRTIO_SCSI_S_SIMPLE;

	/* Set the command CDB */
	memcpy(v_cmd->req.cdb, cmd->cmd, cmd->cmdlen);
	log_debug("cmd %x\n", *cmd->cmd);

	/*
	 * Set up scatter-gather lists for the request and response.
	 * The device writes to IN buffers and reads from OUT buffers.
	 */
	out_sgs[out_count].addr = &v_cmd->req;
	out_sgs[out_count].length = sizeof(v_cmd->req);
	out_count++;

	in_sgs[in_count].addr = &v_cmd->resp;
	in_sgs[in_count].length = sizeof(v_cmd->resp);
	in_count++;

	if (cmd->datalen > 0) {
		switch (cmd->cmd[0]) {
		case SCSI_READ6:
		case SCSI_READ10:
		case SCSI_READ16:
		case SCSI_INQUIRY:
			in_sgs[in_count].addr = cmd->pdata;
			in_sgs[in_count].length = cmd->datalen;
			in_count++;
			break;
		case SCSI_WRITE6:
		case SCSI_WRITE10:
			out_sgs[out_count].addr = cmd->pdata;
			out_sgs[out_count].length = cmd->datalen;
			out_count++;
			break;
		case SCSI_REPORT_LUNS:
		case SCSI_RD_CAPAC:
			in_sgs[in_count].addr = cmd->pdata;
			in_sgs[in_count].length = cmd->datalen;
			in_count++;
			break;
		default:
			printf("Unsupported SCSI command %#02x\n", cmd->cmd[0]);
			return -ENOTSUPP;
		}
	}

	for (i = 0; i < out_count; i++)
		sgs[i] = &out_sgs[i];
	for (i = 0; i < in_count; i++)
		sgs[out_count + i] = &in_sgs[i];

	/* Add the buffers to the request virtqueue */
	len = virtqueue_add(vq, sgs, out_count, in_count);
	if (len < 0) {
		printf("Failed to add buffer to virtqueue: %d\n", len);
		return len;
	}

	virtqueue_kick(vq);

	log_debug("wait...");
	while (virtqueue_get_buf(vq, &dummy_len) != v_cmd)
		;
	log_debug("done\n");

	/* Process the response */
	ret = 0;
	if (v_cmd->resp.response) {
		if (v_cmd->resp.response == VIRTIO_SCSI_S_BAD_TARGET) {
			/*
			 * This is an expected result when scanning for a
			 * non-existent device. Handle it silently.
			 */
			ret = -ENODEV;
		} else {
			log_err("virtio-scsi: response error: %#x\n",
				v_cmd->resp.response);
			ret = -EIO;
		}
	} else {
		ret = v_cmd->resp.status;
		if (ret)
			log_debug("status %x\n", v_cmd->resp.status);
		cmd->sensedatalen = min_t(u32, SENSE_BUF_LEN,
					  v_cmd->resp.sense_len);
		if (cmd->sensedatalen)
			memcpy(cmd->sense_buf, v_cmd->resp.sense,
			       cmd->sensedatalen);
	}

	return ret;
}

static int virtio_scsi_probe(struct udevice *dev)
{
	struct virtio_scsi_priv *priv = dev_get_priv(dev);
	struct virtio_scsi_config config;
	struct scsi_plat *uc_plat;
	int ret;

	/* read the device-specific configuration space */
	virtio_cread_bytes(dev, 0, &config, sizeof(config));
	priv->max_target = config.max_target;

	/* U-Boot only supports up to 8 LUNs */
	priv->max_lun = min(config.max_lun, 7u);

	log_debug("virtio-scsi: max_target=%d, max_lun=%d, sense_size=%d, cdb_size=%d\n",
		  priv->max_target, priv->max_lun, config.sense_size,
		  config.cdb_size);

	/* allocate the virtqueues */
	ret = virtio_find_vqs(dev, QUEUE_COUNT, priv->vqs);
	if (ret) {
		printf("Failed to find vqs\n");
		return -ENOENT;
	}
	uc_plat = dev_get_uclass_plat(dev);
	uc_plat->max_lun = priv->max_lun;
	uc_plat->max_id = priv->max_target;

	return 0;
}

static int virtio_scsi_bind(struct udevice *dev)
{
	struct virtio_dev_priv *uc_priv = dev_get_uclass_priv(dev->parent);

	/* Indicate what driver features we support */
	virtio_driver_features_init(uc_priv, features, ARRAY_SIZE(features),
				    NULL, 0);

	return 0;
}

static const struct scsi_ops virtio_scsi_ops = {
	.exec = virtio_scsi_exec,
};

static const struct udevice_id virtio_scsi_ids[] = {
	{ .compatible = "virtio,scsi" },
	{ }
};

U_BOOT_DRIVER(virtio_scsi) = {
	.name		= VIRTIO_SCSI_DRV_NAME,
	.id		= UCLASS_SCSI,
	.of_match	= virtio_scsi_ids,
	.priv_auto	= sizeof(struct virtio_scsi_priv),
	.ops		= &virtio_scsi_ops,
	.probe		= virtio_scsi_probe,
	.remove		= virtio_remove,
	.bind		= virtio_scsi_bind,
	.flags		= DM_FLAG_ACTIVE_DMA,
};
