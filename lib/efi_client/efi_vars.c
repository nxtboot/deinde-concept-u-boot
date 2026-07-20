// SPDX-License-Identifier: GPL-2.0+
/*
 * Aurora Innovation, Inc. Copyright 2022.
 *
 */

#define LOG_CATEGORY	LOGC_EFI

#define __efi_runtime

#include <charset.h>
#include <errno.h>
#include <asm/global_data.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_variable.h>

DECLARE_GLOBAL_DATA_PTR;

efi_status_t efi_get_variable_int(const u16 *variable_name, const efi_guid_t *vendor,
				  u32 *attributes, efi_uintn_t *data_size,
				  void *data, u64 *timep)
{
	struct efi_runtime_services *run = efi_get_run();

	return run->get_variable((u16 *)variable_name, vendor, attributes, data_size, data);
}

efi_status_t efi_set_variable_int(const u16 *variable_name, const efi_guid_t *vendor,
				  u32 attributes, efi_uintn_t data_size, const void *data,
				  bool ro_check)
{
	struct efi_runtime_services *run = efi_get_run();

	return run->set_variable((u16 *)variable_name, vendor, attributes, data_size, data);
}

efi_status_t efi_get_next_variable_name_int(efi_uintn_t *variable_name_size,
					    u16 *variable_name, efi_guid_t *vendor)
{
	struct efi_runtime_services *run = efi_get_run();

	return run->get_next_variable_name(variable_name_size, variable_name, vendor);
}

efi_status_t efi_query_variable_info_int(u32 attributes,
					 u64 *maximum_variable_storage_size,
					 u64 *remaining_variable_storage_size,
					 u64 *maximum_variable_size)
{
	struct efi_runtime_services *run = efi_get_run();

	return run->query_variable_info(attributes,
					maximum_variable_storage_size,
					remaining_variable_storage_size,
					maximum_variable_size);
}

const efi_guid_t *efi_auth_var_get_guid(const u16 *name)
{
	if (!u16_strcmp(name, u"db") || !u16_strcmp(name, u"dbx") ||
	    !u16_strcmp(name, u"dbt") || !u16_strcmp(name, u"dbr"))
		return &efi_guid_image_security_database;

	return &efi_global_variable_guid;
}

