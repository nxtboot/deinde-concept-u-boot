// SPDX-License-Identifier: GPL-2.0+
/*
 * EFI device path from u-boot device-model mapping
 *
 * (C) Copyright 2017 Rob Clark
 */

#define LOG_CATEGORY LOGC_EFI

#include <blk.h>
#include <dm.h>
#include <dm/root.h>
#include <efi_device_path.h>
#include <ide.h>
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

/**
 * find_handle() - find handle by device path and installed protocol
 *
 * If @rem is provided, the handle with the longest partial match is returned.
 *
 * @dp:		device path to search
 * @guid:	GUID of protocol that must be installed on path or NULL
 * @short_path:	use short form device path for matching
 * @rem:	pointer to receive remaining device path
 * Return:	matching handle
 */
static efi_handle_t find_handle(struct efi_device_path *dp,
				const efi_guid_t *guid, bool short_path,
				struct efi_device_path **rem)
{
	efi_handle_t handle, best_handle = NULL;
	efi_uintn_t len, best_len = 0;

	len = efi_dp_instance_size(dp);

	list_for_each_entry(handle, &efis->bs.obj_list, link) {
		struct efi_handler *handler;
		struct efi_device_path *dp_current;
		efi_uintn_t len_current;
		efi_status_t ret;

		if (guid) {
			ret = efi_search_protocol(handle, guid, &handler);
			if (ret != EFI_SUCCESS)
				continue;
		}
		ret = efi_search_protocol(handle, &efi_guid_device_path,
					  &handler);
		if (ret != EFI_SUCCESS)
			continue;
		dp_current = handler->protocol_interface;
		if (short_path) {
			dp_current = efi_dp_shorten(dp_current);
			if (!dp_current)
				continue;
		}
		len_current = efi_dp_instance_size(dp_current);
		if (rem) {
			if (len_current > len)
				continue;
		} else {
			if (len_current != len)
				continue;
		}
		if (memcmp(dp_current, dp, len_current))
			continue;
		if (!rem)
			return handle;
		if (len_current > best_len) {
			best_len = len_current;
			best_handle = handle;
			*rem = (void*)((u8 *)dp + len_current);
		}
	}
	return best_handle;
}

efi_handle_t efi_dp_find_obj(struct efi_device_path *dp,
			     const efi_guid_t *guid,
			     struct efi_device_path **rem)
{
	efi_handle_t handle;

	handle = find_handle(dp, guid, false, rem);
	if (!handle)
		/* Match short form device path */
		handle = find_handle(dp, guid, true, rem);

	return handle;
}
