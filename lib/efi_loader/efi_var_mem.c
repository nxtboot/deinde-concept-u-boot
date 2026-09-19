// SPDX-License-Identifier: GPL-2.0+
/*
 * File interface for UEFI variables
 *
 * Copyright (c) 2020, Heinrich Schuchardt
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_loader.h>
#include <efi_variable.h>
#include <malloc.h>
#include <mapmem.h>
#include <u-boot/crc.h>

static const u16 __efi_runtime_rodata vtf[] = u"VarToFile";

/**
 * efi_var_mem_compare() - compare GUID and name with a variable
 *
 * @var:	variable to compare
 * @guid:	GUID to compare
 * @name:	variable name to compare
 * @next:	pointer to next variable
 * Return:	true if match
 */
static bool __efi_runtime
efi_var_mem_compare(struct efi_var_entry *var, const efi_guid_t *guid,
		    const u16 *name, struct efi_var_entry **next)
{
	int i;
	u8 *guid1, *guid2;
	const u16 *data, *var_name;
	bool match = true;

	for (guid1 = (u8 *)&var->guid, guid2 = (u8 *)guid, i = 0;
	     i < sizeof(efi_guid_t) && match; ++i)
		match = (guid1[i] == guid2[i]);

	for (data = var->name, var_name = name;; ++data) {
		if (match)
			match = (*data == *var_name);
		if (!*data)
			break;
		if (*var_name)
			++var_name;
	}

	++data;

	if (next)
		*next = (struct efi_var_entry *)
			ALIGN((uintptr_t)data + var->length, 8);

	if (match)
		efis->var.current = var;

	return match;
}

/**
 * efi_var_entry_len() - Get the entry len including headers & name
 *
 * @var:	pointer to variable start
 *
 * Return:	8-byte aligned variable entry length
 */

u32 __efi_runtime efi_var_entry_len(struct efi_var_entry *var)
{
	if (!var)
		return 0;

	return ALIGN((sizeof(u16) * (u16_strlen(var->name) + 1)) +
		     var->length + sizeof(*var), 8);
}

struct efi_var_entry __efi_runtime
*efi_var_mem_find(const efi_guid_t *guid, const u16 *name,
		  struct efi_var_entry **next)
{
	const struct efi_var *evar = &efis->var;
	struct efi_var_entry *var, *last;

	last = (struct efi_var_entry *)
	       ((uintptr_t)evar->buf + evar->buf->length);

	if (!*name) {
		if (next) {
			*next = evar->buf->var;
			if (*next >= last)
				*next = NULL;
		}
		return NULL;
	}
	if (evar->current &&
	    efi_var_mem_compare(evar->current, guid, name, next)) {
		if (next && *next >= last)
			*next = NULL;
		return evar->current;
	}

	var = evar->buf->var;
	if (var < last) {
		for (; var;) {
			struct efi_var_entry *pos;
			bool match;

			match = efi_var_mem_compare(var, guid, name, &pos);
			if (pos >= last)
				pos = NULL;
			if (match) {
				if (next)
					*next = pos;
				return var;
			}
			var = pos;
		}
	}
	if (next)
		*next = NULL;
	return NULL;
}

void __efi_runtime efi_var_mem_del(struct efi_var_entry *var)
{
	struct efi_var *evar = &efis->var;
	u16 *data;
	struct efi_var_entry *next, *last;

	if (!var)
		return;

	last = (struct efi_var_entry *)
	       ((uintptr_t)evar->buf + evar->buf->length);
	if (var <= evar->current)
		evar->current = NULL;

	for (data = var->name; *data; ++data)
		;
	++data;
	next = (struct efi_var_entry *)
	       ALIGN((uintptr_t)data + var->length, 8);
	evar->buf->length -= (uintptr_t)next - (uintptr_t)var;

	/* efi_memcpy_runtime() can be used because next >= var. */
	efi_memcpy_runtime(var, next, (uintptr_t)last - (uintptr_t)next);
	evar->buf->crc32 = crc32(0, (u8 *)evar->buf->var,
				 evar->buf->length -
				 sizeof(struct efi_var_file));
}

efi_status_t __efi_runtime efi_var_mem_ins(
				const u16 *variable_name,
				const efi_guid_t *vendor, u32 attributes,
				const efi_uintn_t size1, const void *data1,
				const efi_uintn_t size2, const void *data2,
				const u64 time, bool *changep)
{
	struct efi_var *evar = &efis->var;
	u16 *data;
	struct efi_var_entry *var;
	u32 var_name_len;

	if (changep)
		*changep = true;

	/*
	 * If this is not an append (size2 == 0), check whether the variable
	 * already exists with identical attributes and data. When nothing
	 * changed we can skip the write and avoid superfluous erases.
	 */
	if (!size2 && changep) {
		struct efi_var_entry *old;

		old = efi_var_mem_find(vendor, variable_name, NULL);
		if (old && old->attr == attributes &&
		    old->length == size1 && old->time == time) {
			u16 *old_data;

			for (old_data = old->name; *old_data; ++old_data)
				;
			++old_data;

			if (!efi_memcmp_runtime(old_data, data1, size1)) {
				*changep = false;
				return EFI_SUCCESS;
			}
		}
	}

	var = (struct efi_var_entry *)
	      ((uintptr_t)evar->buf + evar->buf->length);
	var_name_len = u16_strlen(variable_name) + 1;
	data = var->name + var_name_len;

	if ((uintptr_t)data - (uintptr_t)evar->buf + size1 + size2 >
	    EFI_VAR_BUF_SIZE)
		return EFI_OUT_OF_RESOURCES;

	var->attr = attributes;
	var->length = size1 + size2;
	var->time = time;

	efi_memcpy_runtime(&var->guid, vendor, sizeof(efi_guid_t));
	efi_memcpy_runtime(var->name, variable_name,
			   sizeof(u16) * var_name_len);
	efi_memcpy_runtime(data, data1, size1);
	efi_memcpy_runtime((u8 *)data + size1, data2, size2);

	var = (struct efi_var_entry *)
	      ALIGN((uintptr_t)data + var->length, 8);
	evar->buf->length = (uintptr_t)var - (uintptr_t)evar->buf;
	evar->buf->crc32 = crc32(0, (u8 *)evar->buf->var,
				 evar->buf->length -
				 sizeof(struct efi_var_file));

	return EFI_SUCCESS;
}

u64 __efi_runtime efi_var_mem_free(void)
{
	const struct efi_var *evar = &efis->var;

	if (evar->buf->length + sizeof(struct efi_var_entry) >=
	    EFI_VAR_BUF_SIZE)
		return 0;

	return EFI_VAR_BUF_SIZE - evar->buf->length -
	       sizeof(struct efi_var_entry);
}

/**
 * efi_var_mem_notify_exit_boot_services() - SetVirtualMemoryMap callback
 *
 * @event:	callback event
 * @context:	callback context
 */
static void EFIAPI __efi_runtime
efi_var_mem_notify_virtual_address_map(struct efi_event *event, void *context)
{
	struct efi_var *evar = &efis->var;

	efi_convert_pointer(0, (void **)&evar->buf);
	evar->current = NULL;
}

/**
 * efi_var_mem_recover() - Keep a copy of a store left behind by the last boot
 *
 * With a fixed buffer address, memory which survived a warm reset may still
 * hold the variable store as the OS left it, including changes it made at
 * runtime. If the buffer holds a valid store, copy it so that
 * efi_var_mem_recovered() can hand it out once the variable services are up.
 */
static void efi_var_mem_recover(void)
{
	struct efi_var *evar = &efis->var;
	struct efi_var_file *buf = evar->buf;

	if (buf->reserved || buf->magic != EFI_VAR_FILE_MAGIC ||
	    buf->length > EFI_VAR_BUF_SIZE ||
	    buf->length < sizeof(struct efi_var_file) ||
	    buf->crc32 != crc32(0, (u8 *)buf->var,
				buf->length - sizeof(struct efi_var_file)))
		return;

	evar->recovered = malloc(buf->length);
	if (evar->recovered)
		memcpy(evar->recovered, buf, buf->length);
}

struct efi_var_file __efi_runtime *efi_var_mem_get_buf(void)
{
	return efis->var.buf;
}

struct efi_var_file *efi_var_mem_recovered(void)
{
	struct efi_var *evar = &efis->var;
	struct efi_var_file *buf = evar->recovered;

	evar->recovered = NULL;

	return buf;
}

efi_status_t efi_var_mem_init(void)
{
	struct efi_var *evar = &efis->var;
	u64 memory = EFI_VAR_BUF_ADDR;
	efi_status_t ret;
	struct efi_event *event;

	ret = efi_allocate_pages(memory ? EFI_ALLOCATE_ADDRESS :
				 EFI_ALLOCATE_ANY_PAGES,
				 EFI_RUNTIME_SERVICES_DATA,
				 efi_size_in_pages(EFI_VAR_BUF_SIZE),
				 &memory);
	if (ret != EFI_SUCCESS)
		return ret;

	evar->buf = map_sysmem(memory, EFI_VAR_BUF_SIZE);
	if (EFI_VAR_BUF_ADDR)
		efi_var_mem_recover();
	memset(evar->buf, '\0', EFI_VAR_BUF_SIZE);
	evar->buf->magic = EFI_VAR_FILE_MAGIC;
	evar->buf->length = (uintptr_t)evar->buf->var -
			      (uintptr_t)evar->buf;

	ret = efi_create_event(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_CALLBACK,
			       efi_var_mem_notify_virtual_address_map, NULL,
			       NULL, &event);
	if (ret != EFI_SUCCESS)
		return ret;
	return ret;
}

/**
 * efi_var_collect_mem() - Copy EFI variables matching attributes mask from
 *                         efis->var.buf
 *
 * @buf:	buffer containing variable collection
 * @lenp:	buffer length
 * @mask:	mask of matched attributes
 *
 * Return:	Status code
 */
efi_status_t __efi_runtime
efi_var_collect_mem(struct efi_var_file *buf, efi_uintn_t *lenp, u32 mask)
{
	const struct efi_var *evar = &efis->var;

	static struct efi_var_file __efi_runtime_data hdr = {
		.magic = EFI_VAR_FILE_MAGIC,
	};
	struct efi_var_entry *last, *var, *var_to;

	hdr.length = sizeof(struct efi_var_file);

	var = evar->buf->var;
	last = (struct efi_var_entry *)
	       ((uintptr_t)evar->buf + evar->buf->length);
	if (buf)
		var_to = buf->var;

	while (var < last) {
		u32 len = efi_var_entry_len(var);

		if ((var->attr & mask) != mask) {
			var = (void *)((uintptr_t)var + len);
			continue;
		}

		hdr.length += len;

		if (buf && hdr.length <= *lenp) {
			efi_memcpy_runtime(var_to, var, len);
			var_to = (void *)var_to + len;
		}
		var = (void *)var + len;
	}

	if (!buf && hdr.length <= *lenp) {
		*lenp = hdr.length;
		return EFI_INVALID_PARAMETER;
	}

	if (!buf || hdr.length > *lenp) {
		*lenp = hdr.length;
		return EFI_BUFFER_TOO_SMALL;
	}
	hdr.crc32 = crc32(0, (u8 *)buf->var,
			  hdr.length - sizeof(struct efi_var_file));

	efi_memcpy_runtime(buf, &hdr, sizeof(hdr));
	*lenp = hdr.length;

	return EFI_SUCCESS;
}

efi_status_t __efi_runtime
efi_get_variable_mem(const u16 *variable_name, const efi_guid_t *vendor,
		     u32 *attributes, efi_uintn_t *data_size, void *data,
		     u64 *timep, u32 mask)
{
	efi_uintn_t old_size;
	struct efi_var_entry *var;
	u16 *pdata;

	if (!variable_name || !vendor || !data_size)
		return EFI_INVALID_PARAMETER;
	var = efi_var_mem_find(vendor, variable_name, NULL);
	if (!var)
		return EFI_NOT_FOUND;

	/*
	 * This function is used at runtime to dump EFI variables.
	 * The memory backend we keep around has BS-only variables as
	 * well. At runtime we filter them here
	 */
	if (mask && !((var->attr & mask) == mask))
		return EFI_NOT_FOUND;

	if (attributes)
		*attributes = var->attr;
	if (timep)
		*timep = var->time;

	if (!u16_strcmp(variable_name, vtf))
		return efi_var_collect_mem(data, data_size, EFI_VARIABLE_NON_VOLATILE);

	old_size = *data_size;
	*data_size = var->length;
	if (old_size < var->length)
		return EFI_BUFFER_TOO_SMALL;

	if (!data)
		return EFI_INVALID_PARAMETER;

	for (pdata = var->name; *pdata; ++pdata)
		;
	++pdata;

	efi_memcpy_runtime(data, pdata, var->length);

	return EFI_SUCCESS;
}

efi_status_t __efi_runtime
efi_get_next_variable_name_mem(efi_uintn_t *variable_name_size,
			       u16 *variable_name, efi_guid_t *vendor,
			       u32 mask)
{
	struct efi_var_entry *var;
	efi_uintn_t len, old_size;
	u16 *pdata;

	if (!variable_name_size || !variable_name || !vendor)
		return EFI_INVALID_PARAMETER;

skip:
	len = *variable_name_size >> 1;
	if (u16_strnlen(variable_name, len) == len)
		return EFI_INVALID_PARAMETER;

	if (!efi_var_mem_find(vendor, variable_name, &var) && *variable_name)
		return EFI_INVALID_PARAMETER;

	if (!var)
		return EFI_NOT_FOUND;

	for (pdata = var->name; *pdata; ++pdata)
		;
	++pdata;

	old_size = *variable_name_size;
	*variable_name_size = (uintptr_t)pdata - (uintptr_t)var->name;

	if (old_size < *variable_name_size)
		return EFI_BUFFER_TOO_SMALL;

	efi_memcpy_runtime(variable_name, var->name, *variable_name_size);
	efi_memcpy_runtime(vendor, &var->guid, sizeof(efi_guid_t));

	if (mask && !((var->attr & mask) == mask)) {
		*variable_name_size = old_size;
		goto skip;
	}

	return EFI_SUCCESS;
}

void efi_var_buf_update(struct efi_var_file *var_buf)
{
	memcpy(efis->var.buf, var_buf, EFI_VAR_BUF_SIZE);
}
