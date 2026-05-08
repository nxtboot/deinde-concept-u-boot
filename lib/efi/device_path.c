// SPDX-License-Identifier: GPL-2.0+
/*
 * EFI device path from u-boot device-model mapping
 *
 * (C) Copyright 2017 Rob Clark
 */

#define LOG_CATEGORY LOGC_EFI

#include <blk.h>
#include <bootflow.h>
#include <dm.h>
#include <dm/root.h>
#include <ide.h>
#include <efi_device_path.h>
#include <log.h>
#include <mapmem.h>
#include <net.h>
#include <usb.h>
#include <mmc.h>
#include <nvme.h>
#include <efi_loader.h>
#include <part.h>
#include <u-boot/uuid.h>
#include <asm-generic/unaligned.h>
#include <linux/compat.h> /* U16_MAX */

const efi_guid_t efi_global_variable_guid = EFI_GLOBAL_VARIABLE_GUID;
const efi_guid_t efi_guid_device_path = EFI_DEVICE_PATH_PROTOCOL_GUID;
const efi_guid_t efi_guid_loaded_image = EFI_LOADED_IMAGE_PROTOCOL_GUID;
const efi_guid_t efi_guid_simple_pointer = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
const efi_guid_t efi_guid_absolute_pointer = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
const efi_guid_t efi_guid_loaded_image_device_path =
		EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID;
const efi_guid_t efi_simple_file_system_protocol_guid =
		EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
const efi_guid_t efi_file_info_guid = EFI_FILE_INFO_GUID;
const efi_guid_t efi_u_boot_guid = U_BOOT_GUID;
/* GUID of the device tree table */
const efi_guid_t efi_guid_fdt = EFI_FDT_GUID;
/* GUID of the EFI_DRIVER_BINDING_PROTOCOL */
const efi_guid_t efi_guid_driver_binding_protocol =
			EFI_DRIVER_BINDING_PROTOCOL_GUID;
const efi_guid_t efi_guid_component_name2 = EFI_COMPONENT_NAME2_PROTOCOL_GUID;

const efi_guid_t efi_guid_image_security_database =
		EFI_IMAGE_SECURITY_DATABASE_GUID;

const efi_guid_t efi_shim_lock = EFI_SHIM_LOCK_GUID;

/* template EFI_DP_END node: */
const struct efi_device_path EFI_DP_END = {
	.type     = DEVICE_PATH_TYPE_END,
	.sub_type = DEVICE_PATH_SUB_TYPE_END,
	.length   = sizeof(EFI_DP_END),
};

/*
 * Determine if an MMC device is an SD card.
 *
 * @desc	block device descriptor
 * Return:	true if the device is an SD card
 */
static bool is_sd(struct blk_desc *desc)
{
	struct mmc *mmc;

	if (!IS_ENABLED(CONFIG_MMC))
		return false;

	mmc = find_mmc_device(desc->devnum);
	if (!mmc)
		return false;

	return IS_SD(mmc) != 0U;
}

struct efi_device_path *efi_dp_next(const struct efi_device_path *dp)
{
	if (!dp)
		return NULL;
	if (dp->type == DEVICE_PATH_TYPE_END)
		return NULL;
	dp = ((void *)dp) + dp->length;
	if (dp->type == DEVICE_PATH_TYPE_END)
		return NULL;
	return (struct efi_device_path *)dp;
}

int efi_dp_match(const struct efi_device_path *a,
		 const struct efi_device_path *b)
{
	while (1) {
		int ret;

		ret = memcmp(&a->length, &b->length, sizeof(a->length));
		if (ret)
			return ret;

		ret = memcmp(a, b, a->length);
		if (ret)
			return ret;

		a = efi_dp_next(a);
		b = efi_dp_next(b);

		if (!a || !b)
			return 0;
	}
}

struct efi_device_path *efi_dp_shorten(struct efi_device_path *dp)
{
	while (dp) {
		if (EFI_DP_TYPE(dp, MESSAGING_DEVICE, MSG_USB_WWI) ||
		    EFI_DP_TYPE(dp, MEDIA_DEVICE, HARD_DRIVE_PATH) ||
		    EFI_DP_TYPE(dp, MEDIA_DEVICE, FILE_PATH))
			return dp;

		dp = efi_dp_next(dp);
	}

	return dp;
}

const struct efi_device_path *efi_dp_last_node(const struct efi_device_path *dp)
{
	struct efi_device_path *ret;

	if (!dp || dp->type == DEVICE_PATH_TYPE_END)
		return NULL;
	while (dp) {
		ret = (struct efi_device_path *)dp;
		dp = efi_dp_next(dp);
	}
	return ret;
}

efi_uintn_t efi_dp_instance_size(const struct efi_device_path *dp)
{
	efi_uintn_t sz = 0;

	if (!dp || dp->type == DEVICE_PATH_TYPE_END)
		return 0;
	while (dp) {
		sz += dp->length;
		dp = efi_dp_next(dp);
	}

	return sz;
}

efi_uintn_t efi_dp_size(const struct efi_device_path *dp)
{
	const struct efi_device_path *p = dp;

	if (!p)
		return 0;
	while (p->type != DEVICE_PATH_TYPE_END ||
	       p->sub_type != DEVICE_PATH_SUB_TYPE_END)
		p = (void *)p + p->length;

	return (void *)p - (void *)dp;
}

struct efi_device_path *efi_dp_dup(const struct efi_device_path *dp)
{
	struct efi_device_path *ndp;
	size_t sz = efi_dp_size(dp) + sizeof(EFI_DP_END);

	if (!dp)
		return NULL;

	ndp = efi_alloc(sz);
	if (!ndp)
		return NULL;
	memcpy(ndp, dp, sz);

	return ndp;
}

struct
efi_device_path *efi_dp_concat(const struct efi_device_path *dp1,
			       const struct efi_device_path *dp2,
			       size_t split_end_node)
{
	struct efi_device_path *ret;
	size_t end_size;

	if (!dp1 && !dp2) {
		/* return an end node */
		ret = efi_dp_dup(&EFI_DP_END);
	} else if (!dp1) {
		ret = efi_dp_dup(dp2);
	} else if (!dp2) {
		ret = efi_dp_dup(dp1);
	} else {
		/* both dp1 and dp2 are non-null */
		size_t sz1;
		size_t sz2 = efi_dp_size(dp2);
		void *p;

		if (split_end_node < sizeof(struct efi_device_path))
			sz1 = efi_dp_size(dp1);
		else
			sz1 = split_end_node;

		if (split_end_node)
			end_size = 2 * sizeof(EFI_DP_END);
		else
			end_size = sizeof(EFI_DP_END);
		p = efi_alloc(sz1 + sz2 + end_size);
		if (!p)
			return NULL;
		ret = p;
		memcpy(p, dp1, sz1);
		p += sz1;

		if (split_end_node) {
			memcpy(p, &EFI_DP_END, sizeof(EFI_DP_END));
			p += sizeof(EFI_DP_END);
		}

		/* the end node of the second device path has to be retained */
		memcpy(p, dp2, sz2);
		p += sz2;
		memcpy(p, &EFI_DP_END, sizeof(EFI_DP_END));
	}

	return ret;
}

struct efi_device_path *efi_dp_append_node(const struct efi_device_path *dp,
					   const struct efi_device_path *node)
{
	struct efi_device_path *ret;

	if (!node && !dp) {
		ret = efi_dp_dup(&EFI_DP_END);
	} else if (!node) {
		ret = efi_dp_dup(dp);
	} else if (!dp) {
		size_t sz = node->length;
		void *p = efi_alloc(sz + sizeof(EFI_DP_END));

		if (!p)
			return NULL;
		memcpy(p, node, sz);
		memcpy(p + sz, &EFI_DP_END, sizeof(EFI_DP_END));
		ret = p;
	} else {
		/* both dp and node are non-null */
		size_t sz = efi_dp_size(dp);
		void *p = efi_alloc(sz + node->length + sizeof(EFI_DP_END));

		if (!p)
			return NULL;
		memcpy(p, dp, sz);
		memcpy(p + sz, node, node->length);
		memcpy(p + sz + node->length, &EFI_DP_END, sizeof(EFI_DP_END));
		ret = p;
	}

	return ret;
}

struct efi_device_path *efi_dp_create_device_node(const u8 type,
						  const u8 sub_type,
						  const u16 length)
{
	struct efi_device_path *ret;

	if (length < sizeof(struct efi_device_path))
		return NULL;

	ret = efi_alloc(length);
	if (!ret)
		return ret;
	ret->type = type;
	ret->sub_type = sub_type;
	ret->length = length;
	return ret;
}

struct efi_device_path *
efi_dp_append_instance(const struct efi_device_path *dp,
		       const struct efi_device_path *dpi)
{
	size_t sz, szi;
	struct efi_device_path *p, *ret;

	if (!dpi)
		return NULL;
	if (!dp)
		return efi_dp_dup(dpi);
	sz = efi_dp_size(dp);
	szi = efi_dp_instance_size(dpi);
	p = efi_alloc(sz + szi + 2 * sizeof(EFI_DP_END));
	if (!p)
		return NULL;
	ret = p;
	memcpy(p, dp, sz + sizeof(EFI_DP_END));
	p = (void *)p + sz;
	p->sub_type = DEVICE_PATH_SUB_TYPE_INSTANCE_END;
	p = (void *)p + sizeof(EFI_DP_END);
	memcpy(p, dpi, szi);
	p = (void *)p + szi;
	memcpy(p, &EFI_DP_END, sizeof(EFI_DP_END));
	return ret;
}

struct efi_device_path *efi_dp_get_next_instance(struct efi_device_path **dp,
						 efi_uintn_t *size)
{
	size_t sz;
	struct efi_device_path *p;

	if (size)
		*size = 0;
	if (!dp || !*dp)
		return NULL;
	sz = efi_dp_instance_size(*dp);
	p = efi_alloc(sz + sizeof(EFI_DP_END));
	if (!p)
		return NULL;
	memcpy(p, *dp, sz + sizeof(EFI_DP_END));
	*dp = (void *)*dp + sz;
	if ((*dp)->sub_type == DEVICE_PATH_SUB_TYPE_INSTANCE_END)
		*dp = (void *)*dp + sizeof(EFI_DP_END);
	else
		*dp = NULL;
	if (size)
		*size = sz + sizeof(EFI_DP_END);
	return p;
}

bool efi_dp_is_multi_instance(const struct efi_device_path *dp)
{
	const struct efi_device_path *p = dp;

	if (!p)
		return false;
	while (p->type != DEVICE_PATH_TYPE_END)
		p = (void *)p + p->length;
	return p->sub_type == DEVICE_PATH_SUB_TYPE_INSTANCE_END;
}

__maybe_unused static unsigned int dp_size(struct udevice *dev)
{
	uint parent_size, size = 0;

	if (!dev || !dev->driver)
		return sizeof(struct efi_device_path_udevice);

	parent_size = dev_get_parent(dev) ? dp_size(dev_get_parent(dev)) : 0;
	switch (device_get_uclass_id(dev)) {
	case UCLASS_ROOT:
		/* stop traversing parents at this point: */
		return sizeof(struct efi_device_path_udevice);
	case UCLASS_ETH:
		return parent_size + sizeof(struct efi_device_path_mac_addr);
	case UCLASS_BLK:
		switch (dev->parent->uclass->uc_drv->id) {
		case UCLASS_IDE:
			if (IS_ENABLED(CONFIG_IDE))
				size = sizeof(struct efi_device_path_atapi);
			break;
		case UCLASS_SCSI:
			if (IS_ENABLED(CONFIG_SCSI))
				size = sizeof(struct efi_device_path_scsi);
			break;
		case UCLASS_MMC:
			if (IS_ENABLED(CONFIG_MMC))
				size =
				     sizeof(struct efi_device_path_sd_mmc_path);
			break;
		case UCLASS_AHCI:
			if (IS_ENABLED(CONFIG_AHCI) ||
			    IS_ENABLED(CONFIG_SATA))
				size = sizeof(struct efi_device_path_sata);
			break;
		case UCLASS_NVME:
			if (IS_ENABLED(CONFIG_NVME))
				size = sizeof(struct efi_device_path_nvme);
			break;
		case UCLASS_MASS_STORAGE:
			if (IS_ENABLED(CONFIG_USB))
				size =
				      sizeof(struct efi_device_path_controller);
			break;
		case UCLASS_EFI_MEDIA:
			/* EFI app */
			size = sizeof(struct efi_device_path_udevice);
			break;
		default:
			/* UCLASS_BLKMAP, UCLASS_HOST, UCLASS_VIRTIO */
			size = sizeof(struct efi_device_path_udevice);
			break;
		}
		break;
	case UCLASS_MMC:
		if (IS_ENABLED(CONFIG_MMC))
			size = sizeof(struct efi_device_path_sd_mmc_path);
		break;
	case UCLASS_MASS_STORAGE:
	case UCLASS_USB_HUB:
		size = sizeof(struct efi_device_path_usb);
		break;
	case UCLASS_EFI_MEDIA:
		size = sizeof(struct efi_device_path_udevice);
		break;
	default:
		size = sizeof(struct efi_device_path_udevice);
		break;
	}

	return parent_size + size;
}

/*
 * Recursively build a device path.
 *
 * @buf		pointer to the end of the device path
 * @dev		device
 * Return:	pointer to the end of the device path
 */
__maybe_unused static void *dp_fill(void *buf, struct udevice *dev)
{
	enum uclass_id uclass_id;

	if (!dev || !dev->driver)
		return buf;

	uclass_id = device_get_uclass_id(dev);
	if (uclass_id != UCLASS_ROOT)
		buf = dp_fill(buf, dev->parent);

	switch (uclass_id) {
	case UCLASS_ETH:
	if (IS_ENABLED(CONFIG_NETDEVICES)) {
		struct efi_device_path_mac_addr *dp = buf;
		struct eth_pdata *pdata = dev_get_plat(dev);

		dp->dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
		dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_MAC_ADDR;
		dp->dp.length = sizeof(*dp);
		memset(&dp->mac, 0, sizeof(dp->mac));
		/* We only support IPv4 */
		memcpy(&dp->mac, &pdata->enetaddr, ARP_HLEN);
		/* Ethernet */
		dp->if_type = 1;
		return &dp[1];
		}
		break;
	case UCLASS_BLK:
		switch (device_get_uclass_id(dev->parent)) {
		case UCLASS_IDE:
		if (IS_ENABLED(CONFIG_IDE)) {
			struct efi_device_path_atapi *dp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			dp->dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_ATAPI;
			dp->dp.length = sizeof(*dp);
			dp->logical_unit_number = desc->devnum;
			dp->primary_secondary = IDE_BUS(desc->devnum);
			dp->slave_master = desc->devnum %
				(IF_ENABLED_INT(CONFIG_IDE,
						CONFIG_SYS_IDE_MAXDEVICE) /
				 IF_ENABLED_INT(CONFIG_IDE,
						CONFIG_SYS_IDE_MAXBUS));
			return &dp[1];
			}
			break;
		case UCLASS_SCSI:
		if (IS_ENABLED(CONFIG_SCSI)) {
			struct efi_device_path_scsi *dp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			dp->dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_SCSI;
			dp->dp.length = sizeof(*dp);
			dp->logical_unit_number = desc->lun;
			dp->target_id = desc->target;
			return &dp[1];
			}
			break;
		case UCLASS_MMC:
		if (IS_ENABLED(CONFIG_MMC)) {
			struct efi_device_path_sd_mmc_path *sddp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			sddp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
			sddp->dp.sub_type = is_sd(desc) ?
				DEVICE_PATH_SUB_TYPE_MSG_SD :
				DEVICE_PATH_SUB_TYPE_MSG_MMC;
			sddp->dp.length   = sizeof(*sddp);
			sddp->slot_number = dev_seq(dev);
			return &sddp[1];
			}
			break;
		case UCLASS_AHCI:
		if (IS_ENABLED(CONFIG_AHCI) || IS_ENABLED(CONFIG_SATA)) {
			struct efi_device_path_sata *dp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			dp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_SATA;
			dp->dp.length   = sizeof(*dp);
			dp->hba_port = desc->devnum;
			/* default 0xffff implies no port multiplier */
			dp->port_multiplier_port = 0xffff;
			dp->logical_unit_number = desc->lun;
			return &dp[1];
			}
			break;
		case UCLASS_NVME:
		if (IS_ENABLED(CONFIG_NVME)) {
			struct efi_device_path_nvme *dp = buf;
			u32 ns_id;

			dp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_NVME;
			dp->dp.length   = sizeof(*dp);
			nvme_get_namespace_id(dev, &ns_id, dp->eui64);
			memcpy(&dp->ns_id, &ns_id, sizeof(ns_id));
			return &dp[1];
			}
			break;
		case UCLASS_MASS_STORAGE:
		if (IS_ENABLED(CONFIG_USB)) {
			struct blk_desc *desc = dev_get_uclass_plat(dev);
			struct efi_device_path_controller *dp = buf;

			dp->dp.type	= DEVICE_PATH_TYPE_HARDWARE_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_CONTROLLER;
			dp->dp.length	= sizeof(*dp);
			dp->controller_number = desc->lun;
			return &dp[1];
			}
			break;
		case UCLASS_EFI_MEDIA:
		if (IS_ENABLED(CONFIG_EFI_APP)) {
			struct efi_device_path_udevice *dp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			dp->dp.type = DEVICE_PATH_TYPE_HARDWARE_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_VENDOR;
			dp->dp.length = sizeof(*dp);
			memcpy(&dp->guid, &efi_u_boot_guid, sizeof(efi_guid_t));
			dp->uclass_id = (UCLASS_BLK & 0xffff) |
					(desc->uclass_id << 16);
			dp->dev_number = desc->devnum;

			return &dp[1];
		}
			break;
		default: {
			/* UCLASS_BLKMAP, UCLASS_HOST, UCLASS_VIRTIO */
			struct efi_device_path_udevice *dp = buf;
			struct blk_desc *desc = dev_get_uclass_plat(dev);

			dp->dp.type = DEVICE_PATH_TYPE_HARDWARE_DEVICE;
			dp->dp.sub_type = DEVICE_PATH_SUB_TYPE_VENDOR;
			dp->dp.length = sizeof(*dp);
			memcpy(&dp->guid, &efi_u_boot_guid,
			       sizeof(efi_guid_t));
			dp->uclass_id = (UCLASS_BLK & 0xffff) |
					(desc->uclass_id << 16);
			dp->dev_number = desc->devnum;

			return &dp[1];
		}
		break;
	}
	case UCLASS_MMC:
	if (IS_ENABLED(CONFIG_MMC)) {
		struct efi_device_path_sd_mmc_path *sddp = buf;
		struct mmc *mmc = mmc_get_mmc_dev(dev);
		struct blk_desc *desc = mmc_get_blk_desc(mmc);

		sddp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
		sddp->dp.sub_type = is_sd(desc) ?
			DEVICE_PATH_SUB_TYPE_MSG_SD :
			DEVICE_PATH_SUB_TYPE_MSG_MMC;
		sddp->dp.length   = sizeof(*sddp);
		sddp->slot_number = dev_seq(dev);

		return &sddp[1];
	}
		break;
	case UCLASS_MASS_STORAGE:
	case UCLASS_USB_HUB: {
		struct efi_device_path_usb *udp = buf;

		switch (device_get_uclass_id(dev->parent)) {
		case UCLASS_USB_HUB: {
			struct usb_device *udev = dev_get_parent_priv(dev);

			udp->parent_port_number = udev->portnr;
			break;
		}
		default:
			udp->parent_port_number = 0;
		}
		udp->dp.type     = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
		udp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_USB;
		udp->dp.length   = sizeof(*udp);
		udp->usb_interface = 0;

		return &udp[1];
	}
	default: {
		struct efi_device_path_udevice *vdp = buf;

		vdp->dp.type = DEVICE_PATH_TYPE_HARDWARE_DEVICE;
		vdp->dp.sub_type = DEVICE_PATH_SUB_TYPE_VENDOR;
		vdp->dp.length = sizeof(*vdp);
		memcpy(&vdp->guid, &efi_u_boot_guid, sizeof(efi_guid_t));
		vdp->uclass_id = uclass_id;
		vdp->dev_number = dev->seq_;

		return &vdp[1];
	}
	}

	return buf;
}

static uint dp_part_size(struct blk_desc *desc, int part)
{
	struct udevice *dev = desc->bdev;
	uint dpsize;

	dpsize = dp_size(dev);

	if (part == 0) /* the actual disk, not a partition */
		return dpsize;

	if (desc->part_type == PART_TYPE_ISO)
		dpsize += sizeof(struct efi_device_path_cdrom_path);
	else
		dpsize += sizeof(struct efi_device_path_hard_drive_path);

	return dpsize;
}

/*
 * Create a device node for a block device partition.
 *
 * @buf		buffer to which the device path is written
 * @desc	block device descriptor
 * @part	partition number, 0 identifies a block device
 *
 * Return:	pointer to position after the node
 */
static void *dp_part_node(void *buf, struct blk_desc *desc, int part)
{
	struct disk_partition info;
	int ret;

	ret = part_get_info(desc, part, &info);
	if (ret < 0)
		return buf;

	if (desc->part_type == PART_TYPE_ISO) {
		struct efi_device_path_cdrom_path *cddp = buf;

		cddp->boot_entry = part;
		cddp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
		cddp->dp.sub_type = DEVICE_PATH_SUB_TYPE_CDROM_PATH;
		cddp->dp.length = sizeof(*cddp);
		cddp->partition_start = info.start;
		cddp->partition_size = info.size;

		buf = &cddp[1];
	} else {
		struct efi_device_path_hard_drive_path *hddp = buf;

		hddp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
		hddp->dp.sub_type = DEVICE_PATH_SUB_TYPE_HARD_DRIVE_PATH;
		hddp->dp.length = sizeof(*hddp);
		hddp->partition_number = part;
		hddp->partition_start = info.start;
		hddp->partition_end = info.size;
		if (desc->part_type == PART_TYPE_EFI)
			hddp->partmap_type = 2;
		else
			hddp->partmap_type = 1;

		switch (desc->sig_type) {
		case SIG_TYPE_NONE:
		default:
			hddp->signature_type = 0;
			memset(hddp->partition_signature, 0,
			       sizeof(hddp->partition_signature));
			break;
		case SIG_TYPE_MBR:
			hddp->signature_type = 1;
			memset(hddp->partition_signature, 0,
			       sizeof(hddp->partition_signature));
			memcpy(hddp->partition_signature, &desc->mbr_sig,
			       sizeof(desc->mbr_sig));
			break;
		case SIG_TYPE_GUID:
			hddp->signature_type = 2;
			if (uuid_str_to_bin(disk_partition_uuid(&info),
					    hddp->partition_signature,
					    UUID_STR_FORMAT_GUID)) {
				log_warning("Partition %d: invalid GUID %s\n",
					    part, disk_partition_uuid(&info));
			}
			break;
		}

		buf = &hddp[1];
	}

	return buf;
}

/*
 * Create a device path for a block device or one of its partitions.
 *
 * @buf		buffer to which the device path is written
 * @desc	block device descriptor
 * @part	partition number, 0 identifies a block device
 */
static void *dp_part_fill(void *buf, struct blk_desc *desc, int part)
{
	struct udevice *dev = desc->bdev;

	buf = dp_fill(buf, dev);

	if (part == 0) /* the actual disk, not a partition */
		return buf;

	return dp_part_node(buf, desc, part);
}

struct efi_device_path *efi_dp_from_part(struct blk_desc *desc, int part)
{
	void *buf, *start;

	start = efi_alloc(dp_part_size(desc, part) + sizeof(EFI_DP_END));
	if (!start)
		return NULL;

	buf = dp_part_fill(start, desc, part);

	*((struct efi_device_path *)buf) = EFI_DP_END;

	return start;
}

struct efi_device_path *efi_dp_part_node(struct blk_desc *desc, int part)
{
	efi_uintn_t dpsize;
	void *buf;

	if (desc->part_type == PART_TYPE_ISO)
		dpsize = sizeof(struct efi_device_path_cdrom_path);
	else
		dpsize = sizeof(struct efi_device_path_hard_drive_path);
	buf = efi_alloc(dpsize);

	if (buf)
		dp_part_node(buf, desc, part);

	return buf;
}

/**
 * path_to_uefi() - convert UTF-8 path to an UEFI style path
 *
 * Convert UTF-8 path to a UEFI style path (i.e. with backslashes as path
 * separators and UTF-16).
 *
 * @src:	source buffer
 * @uefi:	target buffer, possibly unaligned
 */
static void path_to_uefi(void *uefi, const char *src)
{
	u16 *pos = uefi;

	if (!IS_ENABLED(CONFIG_EFI_APP)) {
		/*
		 * efi_set_bootdev() calls this routine indirectly before the
		 * UEFI subsystem is initialized. So we cannot assume unaligned
		 * access to be enabled.
		 */
		allow_unaligned();
	}

	while (*src) {
		s32 code = utf8_get(&src);

		if (code < 0)
			code = '?';
		else if (code == '/')
			code = '\\';
		utf16_put(code, &pos);
	}
	*pos = 0;
}

struct efi_device_path *efi_dp_from_file(const struct efi_device_path *dp,
					 const char *path)
{
	struct efi_device_path_file_path *fp;
	void *buf, *pos;
	size_t dpsize, fpsize;

	dpsize = efi_dp_size(dp);
	fpsize = sizeof(struct efi_device_path) +
		 2 * (utf8_utf16_strlen(path) + 1);
	if (fpsize > U16_MAX)
		return NULL;

	buf = efi_alloc(dpsize + fpsize + sizeof(EFI_DP_END));
	if (!buf)
		return NULL;

	memcpy(buf, dp, dpsize);
	pos = buf + dpsize;

	/* add file-path: */
	if (*path) {
		fp = pos;
		fp->dp.type = DEVICE_PATH_TYPE_MEDIA_DEVICE;
		fp->dp.sub_type = DEVICE_PATH_SUB_TYPE_FILE_PATH;
		fp->dp.length = (u16)fpsize;
		path_to_uefi(fp->str, path);
		pos += fpsize;
	}

	memcpy(pos, &EFI_DP_END, sizeof(EFI_DP_END));

	return buf;
}

struct efi_device_path *efi_dp_from_uart(void)
{
	void *buf, *pos;
	struct efi_device_path_uart *uart;
	size_t dpsize = dp_size(dm_root()) + sizeof(*uart) + sizeof(EFI_DP_END);

	buf = efi_alloc(dpsize);
	if (!buf)
		return NULL;
	pos = dp_fill(buf, dm_root());
	uart = pos;
	uart->dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
	uart->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_UART;
	uart->dp.length = sizeof(*uart);
	pos += sizeof(*uart);
	memcpy(pos, &EFI_DP_END, sizeof(EFI_DP_END));

	return buf;
}

struct efi_device_path *efi_dp_from_eth(struct udevice *dev)
{
	void *buf, *start;
	uint dpsize = 0;

	assert(dev);

	dpsize += dp_size(dev);

	start = efi_alloc(dpsize + sizeof(EFI_DP_END));
	if (!start)
		return NULL;

	buf = dp_fill(start, dev);

	*((struct efi_device_path *)buf) = EFI_DP_END;

	return start;
}

/**
 * efi_dp_from_ipv4() - set device path from IPv4 address
 *
 * Set the device path to an ethernet device path as provided by
 * efi_dp_from_eth() concatenated with a device path of subtype
 * DEVICE_PATH_SUB_TYPE_MSG_IPV4, and an EFI_DP_END node.
 *
 * @ip:		IPv4 local address
 * @mask:	network mask
 * @srv:	IPv4 remote/server address
 * @dev:	net udevice
 * Return:	pointer to device path, NULL on error
 */
static struct efi_device_path *efi_dp_from_ipv4(struct efi_ipv4_address *ip,
						struct efi_ipv4_address *mask,
						struct efi_ipv4_address *srv,
						struct udevice *dev)
{
	struct efi_device_path *dp1, *dp2, *pos;
	struct {
		struct efi_device_path_ipv4 ipv4dp;
		struct efi_device_path end;
	} dp;

	memset(&dp.ipv4dp, 0, sizeof(dp.ipv4dp));
	dp.ipv4dp.dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
	dp.ipv4dp.dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_IPV4;
	dp.ipv4dp.dp.length = sizeof(dp.ipv4dp);
	dp.ipv4dp.protocol = 6;
	if (ip)
		memcpy(&dp.ipv4dp.local_ip_address, ip, sizeof(*ip));
	if (mask)
		memcpy(&dp.ipv4dp.subnet_mask, mask, sizeof(*mask));
	if (srv)
		memcpy(&dp.ipv4dp.remote_ip_address, srv, sizeof(*srv));
	pos = &dp.end;
	memcpy(pos, &EFI_DP_END, sizeof(EFI_DP_END));

	dp1 = efi_dp_from_eth(dev);
	if (!dp1)
		return NULL;

	dp2 = efi_dp_concat(dp1, (const struct efi_device_path *)&dp, 0);

	efi_free_pool(dp1);

	return dp2;
}

struct efi_device_path *efi_dp_from_http(const char *server, struct udevice *dev)
{
	struct efi_device_path *dp1, *dp2;
	struct efi_device_path_uri *uridp;
	efi_uintn_t uridp_len;
	char *pos;
	char tmp[128];
	struct efi_ipv4_address ip = { .ip_addr = { 0, 0, 0, 0 } };
	struct efi_ipv4_address mask = { .ip_addr = { 0, 0, 0, 0 } };

	if ((server && strlen("http://") + strlen(server) + 1  > sizeof(tmp)) ||
	    (!server && IS_ENABLED(CONFIG_NET_LWIP)))
		return NULL;

	efi_net_get_addr(&ip, &mask, NULL, dev);

	dp1 = efi_dp_from_ipv4(&ip, &mask, NULL, dev);
	if (!dp1)
		return NULL;

	strcpy(tmp, "http://");

	if (server) {
		strlcat(tmp, server, sizeof(tmp));
#if !IS_ENABLED(CONFIG_NET_LWIP)
	}
	else if (IS_ENABLED(CONFIG_NET)) {
		ip_to_string(net_server_ip, tmp + strlen("http://"));
#endif
	}

	uridp_len = sizeof(struct efi_device_path) + strlen(tmp) + 1;
	uridp = efi_alloc(uridp_len + sizeof(EFI_DP_END));
	if (!uridp) {
		log_err("Out of memory\n");
		return NULL;
	}
	uridp->dp.type = DEVICE_PATH_TYPE_MESSAGING_DEVICE;
	uridp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MSG_URI;
	uridp->dp.length = uridp_len;
	debug("device path: setting uri device path to %s\n", tmp);
	memcpy(uridp->uri, tmp, strlen(tmp) + 1);

	pos = (char *)uridp + uridp_len;
	memcpy(pos, &EFI_DP_END, sizeof(EFI_DP_END));

	dp2 = efi_dp_concat(dp1, (const struct efi_device_path *)uridp, 0);

	efi_free_pool(uridp);
	efi_free_pool(dp1);

	return dp2;
}

/* Construct a device-path for memory-mapped image */
struct efi_device_path *efi_dp_from_mem(u32 memory_type, void *start_ptr,
					size_t size)
{
	struct efi_device_path_memory *mdp;
	void *buf, *start;

	start = efi_alloc(sizeof(*mdp) + sizeof(EFI_DP_END));
	if (!start)
		return NULL;

	mdp = start;
	mdp->dp.type = DEVICE_PATH_TYPE_HARDWARE_DEVICE;
	mdp->dp.sub_type = DEVICE_PATH_SUB_TYPE_MEMORY;
	mdp->dp.length = sizeof(*mdp);
	mdp->memory_type = memory_type;
	mdp->start_address = (uintptr_t)start_ptr;
	mdp->end_address = mdp->start_address + size;
	buf = &mdp[1];

	*((struct efi_device_path *)buf) = EFI_DP_END;

	return start;
}

/**
 * efi_dp_split_file_path() - split of relative file path from device path
 *
 * Given a device path indicating a file on a device, separate the device
 * path in two: the device path of the actual device and the file path
 * relative to this device.
 *
 * @full_path:		device path including device and file path
 * @device_path:	path of the device
 * @file_path:		relative path of the file or NULL if there is none
 * Return:		status code
 */
efi_status_t efi_dp_split_file_path(struct efi_device_path *full_path,
				    struct efi_device_path **device_path,
				    struct efi_device_path **file_path)
{
	struct efi_device_path *p, *dp, *fp = NULL;

	*device_path = NULL;
	*file_path = NULL;
	dp = efi_dp_dup(full_path);
	if (!dp)
		return EFI_OUT_OF_RESOURCES;
	p = dp;
	while (!EFI_DP_TYPE(p, MEDIA_DEVICE, FILE_PATH)) {
		p = efi_dp_next(p);
		if (!p)
			goto out;
	}
	fp = efi_dp_dup(p);
	if (!fp)
		return EFI_OUT_OF_RESOURCES;
	p->type = DEVICE_PATH_TYPE_END;
	p->sub_type = DEVICE_PATH_SUB_TYPE_END;
	p->length = sizeof(*p);

out:
	*device_path = dp;
	*file_path = fp;
	return EFI_SUCCESS;
}

/**
 * efi_dp_from_efi_app() - create device path for EFI app
 *
 * Create a device path for EFI applications using firmware device paths
 * from EFI media devices
 *
 * @devnr:	device number string (format: "dev:part")
 * @descp:	pointer to store block device descriptor
 * @partp:	pointer to store partition number
 * @dp:		pointer to store created device path
 * Return:	U-Boot error code (0 on success, negative on error)
 */
static int efi_dp_from_efi_app(const char *devnr,
			       struct blk_desc **descp, int *partp,
			       struct efi_device_path **dpp)
{
	struct efi_media_plat *plat;
	struct efi_device_path *dp;
	struct udevice *media_dev;
	struct blk_desc *desc;
	int part, dev_num;
	char *ep;
	int ret;

	log_debug("using EFI app firmware device path for devnr='%s'\n", devnr);

	/* parse device number from devnr (format: "devnum:part") */
	dev_num = hextoul(devnr, &ep);
	if (*ep != ':') {
		log_err("invalid EFI device format: '%s'\n", devnr);
		return log_msg_ret("eda", -EINVAL);
	}

	/* find the EFI media device */
	ret = uclass_get_device(UCLASS_EFI_MEDIA, dev_num, &media_dev);
	if (ret) {
		log_err("cannot find EFI media device %d\n", dev_num);
		return log_msg_ret("eda", -ENODEV);
	}
	plat = dev_get_plat(media_dev);

	log_debug("found EFI media device %d with firmware device path: %pD\n",
		  dev_num, plat->device_path);

	/* use the firmware device path and append partition */
	part = simple_strtoul(ep + 1, NULL, 16);
	if (part > 0) {
		struct efi_device_path *part_dp;
		struct disk_partition pinfo;

		/* Get partition info */
		part = blk_get_device_part_str("efi", devnr, &desc, &pinfo, 1);
		if (part < 0 || !desc) {
			log_err("cannot get partition info for '%s'\n", devnr);
			return log_msg_ret("edb", part < 0 ? part : -ENODEV);
		}

		/* Create partition node */
		part_dp = efi_dp_part_node(desc, part);
		if (!part_dp)
			return log_msg_ret("edn", -ENOMEM);

		/* Combine firmware device path with partition */
		dp = efi_dp_append_node(plat->device_path, part_dp);
		efi_free_pool(part_dp);
	} else {
		/* Use whole device */
		dp = efi_dp_dup(plat->device_path);
	}
	if (!dp)
		return log_msg_ret("ede", -ENOMEM);

	log_debug("created final device path: %pD\n", dp);
	*descp = desc;
	*partp = part;
	*dpp = dp;

	return 0;
}

/**
 * efi_dp_from_name() - convert U-Boot device and file path to device path
 *
 * @dev:	U-Boot device, e.g. 'mmc'
 * @devnr:	U-Boot device number, e.g. 1 for 'mmc:1'
 * @path:	file path relative to U-Boot device, may be NULL
 * @device:	pointer to receive device path of the device
 * @file:	pointer to receive device path for the file
 * Return:	status code
 */
efi_status_t efi_dp_from_name(const char *dev, const char *devnr,
			      const char *path,
			      struct efi_device_path **device,
			      struct efi_device_path **file)
{
	struct blk_desc *desc = NULL;
	struct efi_device_path *dp;
	struct disk_partition fs_partition;
	size_t image_size;
	void *image_ptr;
	int part = 0;

	if (path && !file)
		return EFI_INVALID_PARAMETER;

	if (IS_ENABLED(CONFIG_EFI_LOADER) &&
	    (!strcmp(dev, "Mem") || !strcmp(dev, "hostfs")))  {
		/* loadm command and semihosting */
		efi_get_image_parameters(&image_ptr, &image_size);

		dp = efi_dp_from_mem(EFI_RESERVED_MEMORY_TYPE, image_ptr,
				     image_size);
	} else if (IS_ENABLED(CONFIG_NETDEVICES) &&
		   (!strcmp(dev, "Net") || !strcmp(dev, "Http"))) {
		efi_net_dp_from_dev(&dp, eth_get_dev(), false);
	} else if (!strcmp(dev, "Uart")) {
		dp = efi_dp_from_uart();
	} else if (IS_ENABLED(CONFIG_EFI_APP) && !strcmp(dev, "efi")) {
		int ret;

		ret = efi_dp_from_efi_app(devnr, &desc, &part, &dp);
		if (ret)
			return EFI_INVALID_PARAMETER;
	} else {
		log_debug("calling blk_get_device_part_str dev='%s', devnr='%s'\n",
			  dev, devnr);
		part = blk_get_device_part_str(dev, devnr, &desc, &fs_partition,
					       1);
		if (part < 0 || !desc) {
			log_err("Failed to find fs: dev='%s', devnr='%s', part=%d, desc=%p\n",
				dev, devnr, part, desc);
			return EFI_INVALID_PARAMETER;
		}

		dp = efi_dp_from_part(desc, part);
	}
	if (device)
		*device = dp;

	if (!path)
		return EFI_SUCCESS;

	*file = efi_dp_from_file(dp, path);
	if (!*file)
		return EFI_OUT_OF_RESOURCES;

	return EFI_SUCCESS;
}

/**
 * efi_dp_check_length() - check length of a device path
 *
 * @dp:		pointer to device path
 * @maxlen:	maximum length of the device path
 * Return:
 * * length of the device path if it is less or equal @maxlen
 * * -1 if the device path is longer then @maxlen
 * * -1 if a device path node has a length of less than 4
 * * -EINVAL if maxlen exceeds SSIZE_MAX
 */
ssize_t efi_dp_check_length(const struct efi_device_path *dp,
			    const size_t maxlen)
{
	ssize_t ret = 0;
	u16 len;

	if (maxlen > SSIZE_MAX)
		return -EINVAL;
	for (;;) {
		len = dp->length;
		if (len < 4)
			return -1;
		ret += len;
		if (ret > maxlen)
			return -1;
		if (dp->type == DEVICE_PATH_TYPE_END &&
		    dp->sub_type == DEVICE_PATH_SUB_TYPE_END)
			return ret;
		dp = (const struct efi_device_path *)((const u8 *)dp + len);
	}
}

/**
 * efi_dp_from_lo() - get device-path from load option
 *
 * The load options in U-Boot may contain multiple concatenated device-paths.
 * The first device-path indicates the EFI binary to execute. Subsequent
 * device-paths start with a VenMedia node where the GUID identifies the
 * function (initrd or fdt).
 *
 * @lo:		EFI load option containing a valid device path
 * @guid:	GUID identifying device-path or NULL for the EFI binary
 *
 * Return:
 * device path excluding the matched VenMedia node or NULL.
 * Caller must efi_free_pool the returned value.
 */
struct
efi_device_path *efi_dp_from_lo(struct efi_load_option *lo,
				const efi_guid_t *guid)
{
	struct efi_device_path *fp = lo->file_path;
	struct efi_device_path_vendor *vendor;
	int lo_len = lo->file_path_length;

	if (!guid)
		return efi_dp_dup(fp);

	for (; lo_len >=  sizeof(struct efi_device_path);
	     lo_len -= fp->length, fp = (void *)fp + fp->length) {
		if (lo_len < 0 || efi_dp_check_length(fp, lo_len) < 0)
			break;
		if (fp->type != DEVICE_PATH_TYPE_MEDIA_DEVICE ||
		    fp->sub_type != DEVICE_PATH_SUB_TYPE_VENDOR_PATH)
			continue;

		vendor = (struct efi_device_path_vendor *)fp;
		if (!guidcmp(&vendor->guid, guid))
			return efi_dp_dup(efi_dp_next(fp));
	}
	log_debug("VenMedia(%pUl) not found in %ls\n", &guid, lo->label);

	return NULL;
}

/**
 * search_gpt_dp_node() - search gpt device path node
 *
 * @device_path:	device path
 *
 * Return:	pointer to the gpt device path node
 */
struct efi_device_path *search_gpt_dp_node(struct efi_device_path *device_path)
{
	struct efi_device_path *dp = device_path;

	while (dp) {
		if (dp->type == DEVICE_PATH_TYPE_MEDIA_DEVICE &&
		    dp->sub_type == DEVICE_PATH_SUB_TYPE_HARD_DRIVE_PATH) {
			struct efi_device_path_hard_drive_path *hd_dp =
				(struct efi_device_path_hard_drive_path *)dp;

			if (hd_dp->partmap_type == PART_FORMAT_GPT &&
			    hd_dp->signature_type == SIG_TYPE_GUID)
				return dp;
		}
		dp = efi_dp_next(dp);
	}

	return NULL;
}

const char *efi_dp_guess_uclass(struct efi_device_path *device_path,
				enum uclass_id *guessp)
{
	struct efi_device_path *dp = device_path;
	enum uclass_id best_guess = UCLASS_BLK;
	const char *best_name = "blk";

	while (dp) {
		if (dp->type == DEVICE_PATH_TYPE_MESSAGING_DEVICE) {
			switch (dp->sub_type) {
			case DEVICE_PATH_SUB_TYPE_MSG_ATAPI:
				*guessp = UCLASS_IDE;
				return "ide";
			case DEVICE_PATH_SUB_TYPE_MSG_SCSI:
			case DEVICE_PATH_SUB_TYPE_MSG_ISCSI:
				*guessp = UCLASS_SCSI;
				return "scsi";
			case DEVICE_PATH_SUB_TYPE_MSG_FIREWIRE:
			case DEVICE_PATH_SUB_TYPE_MSG_1394:
				*guessp = UCLASS_BLK;
				return "firewire";
			case DEVICE_PATH_SUB_TYPE_MSG_USB:
			case DEVICE_PATH_SUB_TYPE_MSG_USB_CLASS:
			case DEVICE_PATH_SUB_TYPE_MSG_USB_WWI:
				*guessp = UCLASS_USB;
				return "usb";
			case DEVICE_PATH_SUB_TYPE_MSG_I2O:
				*guessp = UCLASS_BLK;
				return "i2o";
			case DEVICE_PATH_SUB_TYPE_MSG_INFINIBAND:
				*guessp = UCLASS_ETH;
				return "infiniband";
			case DEVICE_PATH_SUB_TYPE_MSG_VENDOR:
				*guessp = UCLASS_MISC;
				return "vendor";
			case DEVICE_PATH_SUB_TYPE_MSG_MAC_ADDR:
			case DEVICE_PATH_SUB_TYPE_MSG_IPV4:
			case DEVICE_PATH_SUB_TYPE_MSG_IPV6:
			case DEVICE_PATH_SUB_TYPE_MSG_VLAN:
				*guessp = UCLASS_ETH;
				return "eth";
			case DEVICE_PATH_SUB_TYPE_MSG_UART:
				*guessp = UCLASS_SERIAL;
				return "serial";
			case DEVICE_PATH_SUB_TYPE_MSG_SATA:
				*guessp = UCLASS_AHCI;
				return "ahci";
			case DEVICE_PATH_SUB_TYPE_MSG_FIBRECHAN:
			case DEVICE_PATH_SUB_TYPE_MSG_FIBRECHAN_EX:
				*guessp = UCLASS_SCSI;
				return "fibrechan";
			case DEVICE_PATH_SUB_TYPE_MSG_SAS:
			case DEVICE_PATH_SUB_TYPE_MSG_SAS_EX:
				*guessp = UCLASS_SCSI;
				return "sas";
			case DEVICE_PATH_SUB_TYPE_MSG_NVME:
				*guessp = UCLASS_NVME;
				return "nvme";
			case DEVICE_PATH_SUB_TYPE_MSG_URI:
				*guessp = UCLASS_ETH;
				return "uri";
			case DEVICE_PATH_SUB_TYPE_MSG_UFS:
				*guessp = UCLASS_UFS;
				return "ufs";
			case DEVICE_PATH_SUB_TYPE_MSG_SD:
			case DEVICE_PATH_SUB_TYPE_MSG_MMC:
			case DEVICE_PATH_SUB_TYPE_MSG_EMMC:
				*guessp = UCLASS_MMC;
				return "mmc";
			default:
				break;
			}
		} else if (dp->type == DEVICE_PATH_TYPE_HARDWARE_DEVICE) {
			/* PCI devices could be many things, keep as fallback */
			best_guess = UCLASS_PCI;
			best_name = "pci";
		}
		dp = efi_dp_next(dp);
	}

	*guessp = best_guess;

	return best_name;
}

int efi_dp_from_bootflow(const struct bootflow *bflow,
			 struct efi_device_path **dpp, bool *allocedp)
{
	struct udevice *bdev = bflow->dev;
	struct blk_desc *desc;
	struct udevice *blk;
	int ret;

	if (IS_ENABLED(CONFIG_EFI_APP)) {
		const struct efi_device_path *dpc;

		ret = efi_dp_from_bootdev(bflow->dev, &dpc);
		if (ret)
			return log_msg_ret("dfa", ret);
		*dpp = (struct efi_device_path *)dpc;
		if (allocedp)
			*allocedp = false;
	} else {
		struct efi_device_path *dp;

		if (!allocedp)
			return log_msg_ret("dfb", -EINVAL);
		ret = bootdev_get_sibling_blk(bdev, &blk);
		if (ret)
			return log_msg_ret("dfc", ret);

		desc = dev_get_uclass_plat(blk);
		dp = efi_dp_from_part(desc, bflow->part);
		if (!dp)
			return log_msg_ret("dfd", -ENOMEM);
		*allocedp = true;
		*dpp = dp;
	}

	return 0;
}
