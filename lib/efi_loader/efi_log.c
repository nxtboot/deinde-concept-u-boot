// SPDX-License-Identifier: GPL-2.0+
/*
 * Logging (to memory) of calls from an EFI app
 *
 * Copyright 2024 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_EFI

#include <bloblist.h>
#include <efi_api.h>
#include <efi_log.h>
#include <errno.h>
#include <log.h>
#include <stdarg.h>
#include <time.h>
#include <linux/string.h>

/* names for enum efil_tag (abbreviated to keep output to a single line) */
static const char *tag_name[EFILT_COUNT] = {
	"alloc_pages",
	"free_pages",
	"alloc_pool",
	"free_pool",
	"open_prot",
	"locate_prot",
	"load_image",
	"exit_bootsvc",
	"locate_hdl",
	"locate_hdlbuf",
	"locate_devp",
	"close_prot",
	"inst_prot",
	"uninst_prot",
	"reinst_prot",
	"reg_prot_note",
	"open_prot_info",
	"prots_per_hdl",
	"inst_cfg_tab",
	"next_mono_cnt",
	"stall",
	"set_watchdog",
	"calc_crc32",
	"call",

	"testing",
};

/* names for enum efil_prot */
static const char *const prot_name[EFILP_COUNT] = {
	"",
	"file",
	"simple_fs",
	"block_io",
};

/* member functions of EFI_FILE_PROTOCOL, in enum efil_file_method order */
static const char *const file_method_name[EFILF_COUNT] = {
	"open",
	"close",
	"delete",
	"read",
	"write",
	"getpos",
	"setpos",
	"getinfo",
	"setinfo",
	"flush",
	"open_ex",
	"read_ex",
	"write_ex",
	"flush_ex",
};

/* member functions of the simple-file-system protocol */
static const char *const simple_fs_method_name[EFILS_COUNT] = {
	"open_volume",
};

/* member functions of EFI_BLOCK_IO_PROTOCOL */
static const char *const block_io_method_name[EFILB_COUNT] = {
	"reset",
	"read_blocks",
	"write_blocks",
	"flush_blocks",
};

/* method-name table for each protocol, NULL if it has none */
static const char *const *const prot_method_name[EFILP_COUNT] = {
	NULL,
	file_method_name,
	simple_fs_method_name,
	block_io_method_name,
};

/* number of entries in each protocol's method-name table */
static const uint prot_method_count[EFILP_COUNT] = {
	0,
	EFILF_COUNT,
	EFILS_COUNT,
	EFILB_COUNT,
};

/* names for enum efi_allocate_type  */
static const char *allocate_type_name[EFI_MAX_ALLOCATE_TYPE] = {
	"any-pages",
	"max-addr",
	"alloc-addr",
};

/* names for enum efi_memory_type */
static const char *memory_type_name[EFI_MAX_MEMORY_TYPE] = {
	"reserved",
	"ldr-code",
	"ldr-data",
	"bt-code",
	"bt-data",
	"rt-code",
	"rt-data",
	"convent",
	"unusable",
	"acpi-rec",
	"acpi-nvs",
	"mmap-io",
	"mmap-iop",
	"pal-code",
	"persist",
	"unaccept",
};

/* names for error codes, trying to keep them short */
static const char *error_name[EFI_ERROR_COUNT] = {
	"OK",
	"load",
	"inval_param",
	"unsupported",
	"bad_buf_sz",
	"buf_small",
	"not_ready",
	"device",
	"write_prot",
	"out_of_rsrc",
	"vol_corrupt",
	"vol_full",
	"no_media",
	"media_chg",
	"not_found",
	"no access",
	"no_response",
	"no_mapping",
	"timeout",
	"not_started",
	"already",
	"aborted",
	"icmp",
	"tftp",
	"protocol",
	"bad version",
	"sec_violate",
	"crc_error",
	"end_media",
	"end_file",
	"inval_lang",
	"compromised",
	"ipaddr_busy",
	"http",
};

/* names for enum efi_locate_search_type */
static const char *const locate_search_name[] = {
	"all",
	"by-notify",
	"by-proto",
};

static const char *test_enum_name[EFI_LOG_TEST_COUNT] = {
	"test0",
	"test1",
};

/**
 * copy_guid() - Copy a GUID into a record, or zero it if there is none
 *
 * The caller's GUID need not outlive the call, so records hold a copy
 *
 * @dest: Where to put the GUID
 * @src: GUID to copy, or NULL
 */
static void copy_guid(efi_guid_t *dest, const efi_guid_t *src)
{
	if (src)
		*dest = *src;
	else
		memset(dest, '\0', sizeof(*dest));
}

/**
 * prep_rec() - prepare a new record in the log
 *
 * This creates a new record at the next available position, setting it up ready
 * to hold data. The size and tag are set up.
 *
 * The log is updated so that the next record will start after this one
 *
 * @tag: tag of the EFI call to record
 * @size: Number of bytes in the caller's struct
 * @recp: Set to point to where the caller should add its data
 * Return: Offset of this record (must be passed to finish_rec())
 */
static int prep_rec(enum efil_tag tag, uint str_size, void **recp)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);
	struct efil_rec_hdr *rec_hdr;
	int ofs, size;

	if (!hdr)
		return -ENOENT;
	size = str_size + sizeof(struct efil_rec_hdr);
	if (hdr->upto + size > hdr->size) {
		hdr->missed++;
		return -ENOSPC;
	}

	rec_hdr = (void *)hdr + hdr->upto;
	rec_hdr->size = size;
	rec_hdr->tag = tag;
	rec_hdr->ended = false;
	rec_hdr->start_us = timer_get_us();
	rec_hdr->dur_us = 0;
	*recp = rec_hdr + 1;

	ofs = hdr->upto;
	hdr->upto += size;

	return ofs;
}

/**
 * finish_rec() - Finish a previously started record
 *
 * @ofs: Offset of record to finish
 * @ret: Return code which is to be returned from the EFI function
 * Return: Pointer to the structure where the caller should add its data
 */
static void *finish_rec(int ofs, efi_status_t ret)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);
	struct efil_rec_hdr *rec_hdr;

	if (!hdr || ofs < 0)
		return NULL;
	rec_hdr = (void *)hdr + ofs;
	rec_hdr->ended = true;
	rec_hdr->e_ret = ret;
	rec_hdr->dur_us = timer_get_us() - rec_hdr->start_us;

	return rec_hdr + 1;
}

int efi_logs_open_protocol(efi_handle_t handle, const efi_guid_t *protocol,
			   void **interface, efi_handle_t agent_handle,
			   efi_handle_t controller_handle, u32 attributes)
{
	struct efil_open_protocol *rec;
	int ret;

	ret = prep_rec(EFILT_OPEN_PROTOCOL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->interface = interface;
	rec->agent_handle = agent_handle;
	rec->controller_handle = controller_handle;
	rec->attributes = attributes;
	rec->e_interface = NULL;

	return ret;
}

int efi_loge_open_protocol(int ofs, efi_status_t efi_ret)
{
	struct efil_open_protocol *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->interface)
		rec->e_interface = *rec->interface;

	return 0;
}

int efi_logs_locate_protocol(const efi_guid_t *protocol, void *registration,
			     void **interface)
{
	struct efil_locate_protocol *rec;
	int ret;

	ret = prep_rec(EFILT_LOCATE_PROTOCOL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	copy_guid(&rec->protocol, protocol);
	rec->registration = registration;
	rec->interface = interface;
	rec->e_interface = NULL;

	return ret;
}

int efi_loge_locate_protocol(int ofs, efi_status_t efi_ret)
{
	struct efil_locate_protocol *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->interface)
		rec->e_interface = *rec->interface;

	return 0;
}

int efi_logs_load_image(bool boot_policy, efi_handle_t parent_image,
			void *source_buffer, efi_uintn_t source_size,
			efi_handle_t *image_handle)
{
	struct efil_load_image *rec;
	int ret;

	ret = prep_rec(EFILT_LOAD_IMAGE, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->boot_policy = boot_policy;
	rec->parent_image = parent_image;
	rec->source_buffer = source_buffer;
	rec->source_size = source_size;
	rec->image_handle = image_handle;
	rec->e_image_handle = NULL;

	return ret;
}

int efi_loge_load_image(int ofs, efi_status_t efi_ret)
{
	struct efil_load_image *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->image_handle)
		rec->e_image_handle = *rec->image_handle;

	return 0;
}

int efi_logs_exit_boot_services(efi_handle_t image_handle, efi_uintn_t map_key)
{
	struct efil_exit_boot_services *rec;
	int ret;

	ret = prep_rec(EFILT_EXIT_BOOT_SERVICES, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->image_handle = image_handle;
	rec->map_key = map_key;

	return ret;
}

int efi_loge_exit_boot_services(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_locate_handle(enum efi_locate_search_type search_type,
			   const efi_guid_t *protocol, void *search_key,
			   efi_uintn_t *buffer_size)
{
	struct efil_locate_handle *rec;
	int ret;

	ret = prep_rec(EFILT_LOCATE_HANDLE, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->search_type = search_type;
	copy_guid(&rec->protocol, protocol);
	rec->search_key = search_key;
	rec->buffer_size = buffer_size;
	rec->e_buffer_size = 0;

	return ret;
}

int efi_loge_locate_handle(int ofs, efi_status_t efi_ret)
{
	struct efil_locate_handle *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->buffer_size)
		rec->e_buffer_size = *rec->buffer_size;

	return 0;
}

int efi_logs_locate_handle_buffer(enum efi_locate_search_type search_type,
				  const efi_guid_t *protocol, void *search_key,
				  efi_uintn_t *no_handles)
{
	struct efil_locate_handle_buffer *rec;
	int ret;

	ret = prep_rec(EFILT_LOCATE_HANDLE_BUFFER, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->search_type = search_type;
	copy_guid(&rec->protocol, protocol);
	rec->search_key = search_key;
	rec->no_handles = no_handles;
	rec->e_no_handles = 0;

	return ret;
}

int efi_loge_locate_handle_buffer(int ofs, efi_status_t efi_ret)
{
	struct efil_locate_handle_buffer *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->no_handles)
		rec->e_no_handles = *rec->no_handles;

	return 0;
}

int efi_logs_locate_device_path(const efi_guid_t *protocol, void *device_path,
				efi_handle_t *device)
{
	struct efil_locate_device_path *rec;
	int ret;

	ret = prep_rec(EFILT_LOCATE_DEVICE_PATH, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	copy_guid(&rec->protocol, protocol);
	rec->device_path = device_path;
	rec->device = device;
	rec->e_device = NULL;

	return ret;
}

int efi_loge_locate_device_path(int ofs, efi_status_t efi_ret)
{
	struct efil_locate_device_path *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->device)
		rec->e_device = *rec->device;

	return 0;
}

int efi_logs_close_protocol(efi_handle_t handle, const efi_guid_t *protocol,
			    efi_handle_t agent_handle,
			    efi_handle_t controller_handle)
{
	struct efil_close_protocol *rec;
	int ret;

	ret = prep_rec(EFILT_CLOSE_PROTOCOL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->agent_handle = agent_handle;
	rec->controller_handle = controller_handle;

	return ret;
}

int efi_loge_close_protocol(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_install_protocol_interface(efi_handle_t *handle,
					const efi_guid_t *protocol,
					int protocol_interface_type,
					void *protocol_interface)
{
	struct efil_install_protocol_interface *rec;
	int ret;

	ret = prep_rec(EFILT_INSTALL_PROTOCOL_INTERFACE, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->protocol_interface_type = protocol_interface_type;
	rec->protocol_interface = protocol_interface;
	rec->e_handle = NULL;

	return ret;
}

int efi_loge_install_protocol_interface(int ofs, efi_status_t efi_ret)
{
	struct efil_install_protocol_interface *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->handle)
		rec->e_handle = *rec->handle;

	return 0;
}

int efi_logs_uninstall_protocol_interface(efi_handle_t handle,
					  const efi_guid_t *protocol,
					  void *protocol_interface)
{
	struct efil_uninstall_protocol_interface *rec;
	int ret;

	ret = prep_rec(EFILT_UNINSTALL_PROTOCOL_INTERFACE, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->protocol_interface = protocol_interface;

	return ret;
}

int efi_loge_uninstall_protocol_interface(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_reinstall_protocol_interface(efi_handle_t handle,
					  const efi_guid_t *protocol,
					  void *old_interface,
					  void *new_interface)
{
	struct efil_reinstall_protocol_interface *rec;
	int ret;

	ret = prep_rec(EFILT_REINSTALL_PROTOCOL_INTERFACE, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->old_interface = old_interface;
	rec->new_interface = new_interface;

	return ret;
}

int efi_loge_reinstall_protocol_interface(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_register_protocol_notify(const efi_guid_t *protocol,
				      struct efi_event *event,
				      void **registration)
{
	struct efil_register_protocol_notify *rec;
	int ret;

	ret = prep_rec(EFILT_REGISTER_PROTOCOL_NOTIFY, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	copy_guid(&rec->protocol, protocol);
	rec->event = event;
	rec->registration = registration;
	rec->e_registration = NULL;

	return ret;
}

int efi_loge_register_protocol_notify(int ofs, efi_status_t efi_ret)
{
	struct efil_register_protocol_notify *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->registration)
		rec->e_registration = *rec->registration;

	return 0;
}

int efi_logs_open_protocol_information(efi_handle_t handle,
				       const efi_guid_t *protocol,
				       efi_uintn_t *entry_count)
{
	struct efil_open_protocol_information *rec;
	int ret;

	ret = prep_rec(EFILT_OPEN_PROTOCOL_INFORMATION, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	copy_guid(&rec->protocol, protocol);
	rec->entry_count = entry_count;
	rec->e_entry_count = 0;

	return ret;
}

int efi_loge_open_protocol_information(int ofs, efi_status_t efi_ret)
{
	struct efil_open_protocol_information *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->entry_count)
		rec->e_entry_count = *rec->entry_count;

	return 0;
}

int efi_logs_protocols_per_handle(efi_handle_t handle,
				  efi_uintn_t *protocol_buffer_count)
{
	struct efil_protocols_per_handle *rec;
	int ret;

	ret = prep_rec(EFILT_PROTOCOLS_PER_HANDLE, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->handle = handle;
	rec->protocol_buffer_count = protocol_buffer_count;
	rec->e_protocol_buffer_count = 0;

	return ret;
}

int efi_loge_protocols_per_handle(int ofs, efi_status_t efi_ret)
{
	struct efil_protocols_per_handle *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	if (rec->protocol_buffer_count)
		rec->e_protocol_buffer_count = *rec->protocol_buffer_count;

	return 0;
}

int efi_logs_install_configuration_table(const efi_guid_t *guid, void *table)
{
	struct efil_install_configuration_table *rec;
	int ret;

	ret = prep_rec(EFILT_INSTALL_CONFIGURATION_TABLE, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	copy_guid(&rec->guid, guid);
	rec->table = table;

	return ret;
}

int efi_loge_install_configuration_table(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_get_next_monotonic_count(u64 *count)
{
	struct efil_get_next_monotonic_count *rec;
	int ret;

	ret = prep_rec(EFILT_GET_NEXT_MONOTONIC_COUNT, sizeof(*rec),
		       (void **)&rec);
	if (ret < 0)
		return ret;

	rec->count = count;

	return ret;
}

int efi_loge_get_next_monotonic_count(int ofs, efi_status_t efi_ret)
{
	struct efil_get_next_monotonic_count *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;

	if (rec->count)
		rec->e_count = *rec->count;

	return 0;
}

int efi_logs_stall(u64 microseconds)
{
	struct efil_stall *rec;
	int ret;

	ret = prep_rec(EFILT_STALL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->microseconds = microseconds;

	return ret;
}

int efi_loge_stall(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_set_watchdog_timer(u64 timeout, u64 watchdog_code, u64 data_size,
				u16 *watchdog_data)
{
	struct efil_set_watchdog_timer *rec;
	int ret;

	ret = prep_rec(EFILT_SET_WATCHDOG_TIMER, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->timeout = timeout;
	rec->watchdog_code = watchdog_code;
	rec->data_size = data_size;
	rec->watchdog_data = watchdog_data;

	return ret;
}

int efi_loge_set_watchdog_timer(int ofs, efi_status_t efi_ret)
{
	if (!finish_rec(ofs, efi_ret))
		return -ENOSPC;

	return 0;
}

int efi_logs_calculate_crc32(const void *data, efi_uintn_t data_size,
			     u32 *crc32_p)
{
	struct efil_calculate_crc32 *rec;
	int ret;

	ret = prep_rec(EFILT_CALCULATE_CRC32, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->data = data;
	rec->data_size = data_size;
	rec->crc32_p = crc32_p;

	return ret;
}

int efi_loge_calculate_crc32(int ofs, efi_status_t efi_ret)
{
	struct efil_calculate_crc32 *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;

	if (rec->crc32_p)
		rec->e_crc32 = *rec->crc32_p;

	return 0;
}

int efi_logs_call(enum efil_prot prot, uint method, uint nargs, ...)
{
	struct efil_call *rec;
	va_list args;
	int ret;
	uint i;

	if (nargs > EFIL_CALL_MAX_ARGS)
		nargs = EFIL_CALL_MAX_ARGS;

	ret = prep_rec(EFILT_CALL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->prot = prot;
	rec->method = method;
	rec->nargs = nargs;
	rec->e_arg = 0;

	va_start(args, nargs);
	for (i = 0; i < nargs; i++)
		rec->arg[i] = va_arg(args, u64);
	va_end(args);

	return ret;
}

int efi_loge_call(int ofs, efi_status_t efi_ret, u64 e_arg)
{
	struct efil_call *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;

	rec->e_arg = e_arg;

	return 0;
}

int efi_logs_testing(enum efil_test_t enum_val, efi_uintn_t int_val,
		     void *buffer, u64 *memory)
{
	struct efil_testing *rec;
	int ret;

	ret = prep_rec(EFILT_TESTING, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->enum_val = enum_val;
	rec->int_val = int_val;
	rec->buffer = buffer;
	rec->memory = memory;
	rec->e_buffer = NULL;
	rec->e_memory = 0;

	return ret;
}

int efi_loge_testing(int ofs, efi_status_t efi_ret)
{
	struct efil_testing *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	rec->e_memory = *rec->memory;
	rec->e_buffer = *rec->buffer;

	return 0;
}

int efi_logs_allocate_pages(enum efi_allocate_type type,
			    enum efi_memory_type memory_type, efi_uintn_t pages,
			    u64 *memory)
{
	struct efil_allocate_pages *rec;
	int ret;

	ret = prep_rec(EFILT_ALLOCATE_PAGES, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->type = type;
	rec->memory_type = memory_type;
	rec->pages = pages;
	rec->memory = memory;
	rec->e_memory = 0;

	return ret;
}

int efi_loge_allocate_pages(int ofs, efi_status_t efi_ret)
{
	struct efil_allocate_pages *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	rec->e_memory = *rec->memory;

	return 0;
}

int efi_logs_free_pages(u64 memory, efi_uintn_t pages)
{
	struct efil_free_pages *rec;
	int ret;

	ret = prep_rec(EFILT_FREE_PAGES, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->memory = memory;
	rec->pages = pages;

	return ret;
}

int efi_loge_free_pages(int ofs, efi_status_t efi_ret)
{
	struct efil_allocate_pages *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;

	return 0;
}

int efi_logs_allocate_pool(enum efi_memory_type pool_type, efi_uintn_t size,
			   void **buffer)
{
	struct efil_allocate_pool *rec;
	int ret;

	ret = prep_rec(EFILT_ALLOCATE_POOL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->pool_type = pool_type;
	rec->size = size;
	rec->buffer = buffer;
	rec->e_buffer = NULL;

	return ret;
}

int efi_loge_allocate_pool(int ofs, efi_status_t efi_ret)
{
	struct efil_allocate_pool *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;
	rec->e_buffer = *rec->buffer;

	return 0;
}

int efi_logs_free_pool(void *buffer)
{
	struct efil_free_pool *rec;
	int ret;

	ret = prep_rec(EFILT_FREE_POOL, sizeof(*rec), (void **)&rec);
	if (ret < 0)
		return ret;

	rec->buffer = buffer;

	return ret;
}

int efi_loge_free_pool(int ofs, efi_status_t efi_ret)
{
	struct efil_free_pool *rec;

	rec = finish_rec(ofs, efi_ret);
	if (!rec)
		return -ENOSPC;

	return 0;
}

static void show_enum(const char *const type_name[], int type)
{
	printf("%s ", type_name[type]);
}

static void show_ulong(const char *prompt, ulong val)
{
	printf("%s %lx", prompt, val);
	if (val >= 10)
		printf("/%ld", val);
	printf(" ");
}

static void show_addr(const char *prompt, ulong addr)
{
	printf("%s %lx ", prompt, addr);
}

static void show_ret(efi_status_t ret)
{
	int code;

	code = ret & ~EFI_ERROR_MASK;
	if (code < ARRAY_SIZE(error_name))
		printf("ret %s", error_name[ret]);
	else
		printf("ret %lx", ret);
}

void show_rec(int seq, struct efil_rec_hdr *rec_hdr)
{
	void *start = (void *)rec_hdr + sizeof(struct efil_rec_hdr);

	printf("%3d %12s ", seq, tag_name[rec_hdr->tag]);
	switch (rec_hdr->tag) {
	case EFILT_ALLOCATE_PAGES: {
		struct efil_allocate_pages *rec = start;

		show_enum(allocate_type_name, rec->type);
		show_enum(memory_type_name, rec->memory_type);
		show_ulong("pgs", (ulong)rec->pages);
		show_addr("mem", (ulong)rec->memory);
		if (rec_hdr->ended) {
			show_addr("*mem", rec->e_memory);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_FREE_PAGES: {
		struct efil_free_pages *rec = start;

		show_addr("mem", rec->memory);
		show_ulong("pag", (ulong)rec->pages);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_ALLOCATE_POOL: {
		struct efil_allocate_pool *rec = start;

		show_enum(memory_type_name, rec->pool_type);
		show_ulong("size", (ulong)rec->size);
		show_addr("buf", (ulong)rec->buffer);
		if (rec_hdr->ended) {
			show_addr("*buf",
				  (ulong)map_to_sysmem((void *)rec->e_buffer));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_FREE_POOL: {
		struct efil_free_pool *rec = start;

		show_addr("buf", map_to_sysmem(rec->buffer));
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_OPEN_PROTOCOL: {
		struct efil_open_protocol *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		printf("%pUs ", &rec->protocol);

		/*
		 * HandleProtocol() is implemented by calling OpenProtocol()
		 * with this attribute, which an application is not permitted
		 * to use itself. Say so, since the record is otherwise
		 * indistinguishable from a real OpenProtocol() call
		 */
		if (rec->attributes == EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL)
			printf("(HandleProtocol) ");
		else
			show_ulong("attr", rec->attributes);
		if (rec_hdr->ended) {
			show_addr("*intf",
				  (ulong)map_to_sysmem(rec->e_interface));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_LOCATE_PROTOCOL: {
		struct efil_locate_protocol *rec = start;

		printf("%pUs ", &rec->protocol);
		if (rec->registration)
			show_addr("reg", (ulong)map_to_sysmem(rec->registration));
		if (rec_hdr->ended) {
			show_addr("*intf",
				  (ulong)map_to_sysmem(rec->e_interface));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_LOAD_IMAGE: {
		struct efil_load_image *rec = start;

		printf("%s ", rec->boot_policy ? "bootmgr" : "app");
		show_addr("parent", (ulong)map_to_sysmem(rec->parent_image));
		if (rec->source_buffer) {
			show_addr("src",
				  (ulong)map_to_sysmem(rec->source_buffer));
			show_ulong("size", (ulong)rec->source_size);
		}
		if (rec_hdr->ended) {
			show_addr("*image",
				  (ulong)map_to_sysmem(rec->e_image_handle));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_EXIT_BOOT_SERVICES: {
		struct efil_exit_boot_services *rec = start;

		show_addr("image", (ulong)map_to_sysmem(rec->image_handle));
		show_ulong("key", (ulong)rec->map_key);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_LOCATE_HANDLE: {
		struct efil_locate_handle *rec = start;

		show_enum(locate_search_name, rec->search_type);
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended) {
			show_ulong("*size", (ulong)rec->e_buffer_size);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_LOCATE_HANDLE_BUFFER: {
		struct efil_locate_handle_buffer *rec = start;

		show_enum(locate_search_name, rec->search_type);
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended) {
			show_ulong("*num", (ulong)rec->e_no_handles);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_LOCATE_DEVICE_PATH: {
		struct efil_locate_device_path *rec = start;

		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended) {
			show_addr("*dev", (ulong)map_to_sysmem(rec->e_device));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_CLOSE_PROTOCOL: {
		struct efil_close_protocol *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_INSTALL_PROTOCOL_INTERFACE: {
		struct efil_install_protocol_interface *rec = start;

		printf("%pUs ", &rec->protocol);
		show_addr("intf", (ulong)map_to_sysmem(rec->protocol_interface));
		if (rec_hdr->ended) {
			show_addr("*hdl", (ulong)map_to_sysmem(rec->e_handle));
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_UNINSTALL_PROTOCOL_INTERFACE: {
		struct efil_uninstall_protocol_interface *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_REINSTALL_PROTOCOL_INTERFACE: {
		struct efil_reinstall_protocol_interface *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_REGISTER_PROTOCOL_NOTIFY: {
		struct efil_register_protocol_notify *rec = start;

		printf("%pUs ", &rec->protocol);
		show_addr("evt", (ulong)map_to_sysmem(rec->event));
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_OPEN_PROTOCOL_INFORMATION: {
		struct efil_open_protocol_information *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		printf("%pUs ", &rec->protocol);
		if (rec_hdr->ended) {
			show_ulong("*num", (ulong)rec->e_entry_count);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_PROTOCOLS_PER_HANDLE: {
		struct efil_protocols_per_handle *rec = start;

		show_addr("hdl", (ulong)map_to_sysmem(rec->handle));
		if (rec_hdr->ended) {
			show_ulong("*num",
				   (ulong)rec->e_protocol_buffer_count);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_INSTALL_CONFIGURATION_TABLE: {
		struct efil_install_configuration_table *rec = start;

		printf("%pUs ", &rec->guid);
		show_addr("table", (ulong)map_to_sysmem(rec->table));
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_GET_NEXT_MONOTONIC_COUNT: {
		struct efil_get_next_monotonic_count *rec = start;

		show_addr("count", (ulong)map_to_sysmem(rec->count));
		if (rec_hdr->ended) {
			show_ulong("*count", (ulong)rec->e_count);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_STALL: {
		struct efil_stall *rec = start;

		show_ulong("us", (ulong)rec->microseconds);
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_SET_WATCHDOG_TIMER: {
		struct efil_set_watchdog_timer *rec = start;

		show_ulong("timeout", (ulong)rec->timeout);
		show_ulong("code", (ulong)rec->watchdog_code);
		show_ulong("size", (ulong)rec->data_size);
		show_addr("data", (ulong)map_to_sysmem(rec->watchdog_data));
		if (rec_hdr->ended)
			show_ret(rec_hdr->e_ret);
		break;
	}
	case EFILT_CALCULATE_CRC32: {
		struct efil_calculate_crc32 *rec = start;

		show_addr("data", (ulong)map_to_sysmem((void *)rec->data));
		show_ulong("size", (ulong)rec->data_size);
		if (rec_hdr->ended) {
			show_ulong("*crc32", (ulong)rec->e_crc32);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_CALL: {
		struct efil_call *rec = start;
		const char *const *methods;
		uint i;

		methods = rec->prot < EFILP_COUNT ? prot_method_name[rec->prot]
			: NULL;
		if (methods && rec->method < prot_method_count[rec->prot]) {
			printf("%s.%s ", prot_name[rec->prot],
			       methods[rec->method]);
		} else {
			printf("%d.%d ", rec->prot, rec->method);
		}
		for (i = 0; i < rec->nargs; i++)
			show_ulong("arg", (ulong)rec->arg[i]);
		if (rec_hdr->ended) {
			if (rec->e_arg)
				show_ulong("*arg", (ulong)rec->e_arg);
			show_ret(rec_hdr->e_ret);
		}
		break;
	}
	case EFILT_TESTING: {
		struct efil_testing *rec = start;

		show_enum(test_enum_name, (int)rec->enum_val);
		show_ulong("int", (ulong)rec->int_val);
		show_addr("buf", map_to_sysmem(rec->buffer));
		show_addr("mem", map_to_sysmem(rec->memory));
		if (rec_hdr->ended) {
			show_addr("*buf", (ulong)map_to_sysmem(rec->e_buffer));
			show_addr("*mem", (ulong)rec->e_memory);
			show_ret(rec_hdr->e_ret);
		}
	}
	case EFILT_COUNT:
		break;
	}

	/*
	 * The times come last so that the start of each line depends only on
	 * the call, which keeps two logs of the same boot comparable
	 */
	printf(" [%u", rec_hdr->start_us);
	if (rec_hdr->ended)
		printf(" +%u", rec_hdr->dur_us);
	printf("]\n");
}

int efi_log_show(void)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);
	struct efil_rec_hdr *rec_hdr;
	int i;

	printf("EFI log (size %x)\n", hdr->upto);
	printf("times are [start_us +duration_us] since boot\n");
	if (!hdr)
		return -ENOENT;
	for (i = 0, rec_hdr = (void *)hdr + sizeof(*hdr);
	     (void *)rec_hdr - (void *)hdr < hdr->upto;
	     i++, rec_hdr = (void *)rec_hdr + rec_hdr->size)
		show_rec(i, rec_hdr);
	printf("%d records\n", i);

	return 0;
}

void efi_log_summary(void)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);
	int count[EFILT_COUNT];
	struct efil_rec_hdr *rec_hdr;
	int total, errors, pending;
	int i;

	if (!hdr)
		return;

	memset(count, '\0', sizeof(count));
	total = 0;
	errors = 0;
	pending = 0;
	for (rec_hdr = (void *)hdr + sizeof(*hdr);
	     (void *)rec_hdr - (void *)hdr < hdr->upto;
	     rec_hdr = (void *)rec_hdr + rec_hdr->size) {
		if (rec_hdr->tag < EFILT_COUNT)
			count[rec_hdr->tag]++;
		if (!rec_hdr->ended)
			pending++;
		else if (rec_hdr->e_ret)
			errors++;
		total++;
	}
	if (!total)
		return;

	printf("\nEFI: %d calls", total);
	if (errors)
		printf(", %d returned an error", errors);
	if (pending)
		printf(", %d did not return", pending);
	printf(" (log %x of %x bytes)\n", hdr->upto, hdr->size);
	if (hdr->missed)
		printf("     %d call(s) not recorded: increase CONFIG_EFI_LOG_SIZE\n",
		       hdr->missed);

	for (i = 0; i < EFILT_COUNT; i++) {
		if (count[i])
			printf("     %12s %d\n", tag_name[i], count[i]);
	}
}

int efi_log_reset(void)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);

	if (!hdr)
		return -ENOENT;
	hdr->upto = sizeof(struct efil_hdr);
	hdr->size = CONFIG_EFI_LOG_SIZE;
	hdr->missed = 0;

	return 0;
}

int efi_log_init(void)
{
	struct efil_hdr *hdr;

	hdr = bloblist_add(BLOBLISTT_EFI_LOG, CONFIG_EFI_LOG_SIZE, 0);
	if (!hdr) {
		/*
		 * Return -ENOMEM since we use -ENOSPC to mean that the log is
		 * full
		 */
		log_warning("Failed to setup EFI log\n");
		return log_msg_ret("eli", -ENOMEM);
	}
	efi_log_reset();

	return 0;
}
