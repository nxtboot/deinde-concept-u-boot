// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * EFI debug support
 *
 * Copyright (c) 2025 Ying-Chun Liu, Linaro Ltd. <paul.liu@linaro.org>
 */

#include <efi_loader.h>
#include <linux/sizes.h>
#include <u-boot/crc.h>

#define EFI_DEBUG_TABLE_ENTRY_SIZE  (sizeof(union efi_debug_image_info))

/**
 * efi_initialize_system_table_pointer() - Initialize system table pointer
 *
 * Return:	status code
 */
efi_status_t efi_initialize_system_table_pointer(void)
{
	struct efi_system_table_pointer *systab_pointer;

	/* Allocate efi_system_table_pointer structure with 4MB alignment. */
	systab_pointer = efi_alloc_aligned_pages(sizeof(*systab_pointer),
						 EFI_RUNTIME_SERVICES_DATA,
						 SZ_4M);

	if (!systab_pointer) {
		log_err("Installing EFI system table pointer failed\n");
		return EFI_OUT_OF_RESOURCES;
	}

	systab_pointer->crc32 = 0;

	systab_pointer->signature = EFI_SYSTEM_TABLE_SIGNATURE;
	systab_pointer->efi_system_table_base = (uintptr_t)&efis->systab;
	systab_pointer->crc32 = crc32(0,
				      (const unsigned char *)systab_pointer,
				      sizeof(struct efi_system_table_pointer));
	efis->debug.systab_pointer = systab_pointer;

	return EFI_SUCCESS;
}

/**
 * efi_core_new_debug_image_info_entry() - Add a new efi_loaded_image structure to the
 *                                         efi_debug_image_info table.
 *
 * @image_info_type: type of debug image information
 * @loaded_image:    pointer to the loaded image protocol for the image
 *                   being loaded
 * @image_handle:    image handle for the image being loaded
 *
 * Re-Allocates the table if it's not large enough to accommodate another
 * entry.
 *
 * Return: status code
 **/
efi_status_t efi_core_new_debug_image_info_entry(u32 image_info_type,
						 struct efi_loaded_image *loaded_image,
						 efi_handle_t image_handle)
{
	struct efi_debug *debug = &efis->debug;
	union efi_debug_image_info **table;
	u32 index;
	u32 table_size;
	efi_status_t ret;

	/* Set the flag indicating that we're in the process of updating
	 * the table.
	 */
	debug->table.update_status |=
		EFI_DEBUG_IMAGE_INFO_UPDATE_IN_PROGRESS;

	table = &debug->table.efi_debug_image_info_table;

	if (debug->table.table_size >= debug->max_entries) {
		/* table is full, re-allocate the buffer increasing the size
		 * by 4 KiB.
		 */
		table_size = debug->max_entries * EFI_DEBUG_TABLE_ENTRY_SIZE;

		ret = efi_realloc((void **)table, table_size + EFI_PAGE_SIZE);

		if (ret != EFI_SUCCESS) {
			debug->table.update_status &=
				~EFI_DEBUG_IMAGE_INFO_UPDATE_IN_PROGRESS;
			return ret;
		}

		/* Enlarge the max table entries and set the first empty
		 * entry index to be the original max table entries.
		 */
		debug->max_entries +=
			EFI_PAGE_SIZE / EFI_DEBUG_TABLE_ENTRY_SIZE;
	}

	/* We always put the next entry at the end of the currently consumed
	 * table (i.e. first free entry)
	 */
	index = debug->table.table_size;

	/* Allocate data for new entry. */
	ret = efi_allocate_pool(EFI_BOOT_SERVICES_DATA,
				sizeof(struct efi_debug_image_info_normal),
				(void **)(&(*table)[index].normal_image));
	if (ret == EFI_SUCCESS && (*table)[index].normal_image) {
		/* Update the entry. */
		(*table)[index].normal_image->image_info_type = image_info_type;
		(*table)[index].normal_image->loaded_image_protocol_instance =
			loaded_image;
		(*table)[index].normal_image->image_handle = image_handle;

		/* Increase the number of EFI_DEBUG_IMAGE_INFO elements and
		 * set the debug->table in modified status.
		 */
		debug->table.table_size++;
		debug->table.update_status |=
			EFI_DEBUG_IMAGE_INFO_TABLE_MODIFIED;
	} else {
		log_err("Adding new efi_debug_image_info failed\n");
		return ret;
	}

	debug->table.update_status &=
		~EFI_DEBUG_IMAGE_INFO_UPDATE_IN_PROGRESS;

	return EFI_SUCCESS;
}

/**
 * efi_core_remove_debug_image_info_entry() - Remove an efi_debug_image_info entry.
 *
 * @image_handle:    image handle for the image being removed
 **/
void efi_core_remove_debug_image_info_entry(efi_handle_t image_handle)
{
	struct efi_debug *debug = &efis->debug;
	union efi_debug_image_info *table;
	u32 index;

	debug->table.update_status |=
		EFI_DEBUG_IMAGE_INFO_UPDATE_IN_PROGRESS;

	table = debug->table.efi_debug_image_info_table;

	for (index = 0; index < debug->max_entries; index++) {
		if (table[index].normal_image &&
		    table[index].normal_image->image_handle == image_handle) {
			/* Found a match. Free up the table entry.
			 * Move the tail of the table one slot to the front.
			 */
			efi_free_pool(table[index].normal_image);

			memmove(&table[index],
				&table[index + 1],
				(debug->table.table_size -
				 index - 1) * EFI_DEBUG_TABLE_ENTRY_SIZE);

			/* Decrease the number of EFI_DEBUG_IMAGE_INFO
			 * elements and set the debug->table
			 * in modified status.
			 */
			debug->table.table_size--;
			table[debug->table.table_size].normal_image =
				NULL;
			debug->table.update_status |=
				EFI_DEBUG_IMAGE_INFO_TABLE_MODIFIED;
			break;
		}
	}

	debug->table.update_status &=
		~EFI_DEBUG_IMAGE_INFO_UPDATE_IN_PROGRESS;
}
