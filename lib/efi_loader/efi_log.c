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

/* names for enum efil_tag (abbreviated to keep output to a single line) */
static const char *tag_name[EFILT_COUNT] = {
	"alloc_pages",
	"free_pages",
	"alloc_pool",
	"free_pool",
	"open_prot",

	"testing",
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

static const char *test_enum_name[EFI_LOG_TEST_COUNT] = {
	"test0",
	"test1",
};

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
	if (hdr->upto + size > hdr->size)
		return -ENOSPC;

	rec_hdr = (void *)hdr + hdr->upto;
	rec_hdr->size = size;
	rec_hdr->tag = tag;
	rec_hdr->ended = false;
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
	if (protocol)
		rec->protocol = *protocol;
	else
		memset(&rec->protocol, '\0', sizeof(rec->protocol));
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

static void show_enum(const char *type_name[], int type)
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
	printf("\n");
}

int efi_log_show(void)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);
	struct efil_rec_hdr *rec_hdr;
	int i;

	printf("EFI log (size %x)\n", hdr->upto);
	if (!hdr)
		return -ENOENT;
	for (i = 0, rec_hdr = (void *)hdr + sizeof(*hdr);
	     (void *)rec_hdr - (void *)hdr < hdr->upto;
	     i++, rec_hdr = (void *)rec_hdr + rec_hdr->size)
		show_rec(i, rec_hdr);
	printf("%d records\n", i);

	return 0;
}

int efi_log_reset(void)
{
	struct efil_hdr *hdr = bloblist_find(BLOBLISTT_EFI_LOG, 0);

	if (!hdr)
		return -ENOENT;
	hdr->upto = sizeof(struct efil_hdr);
	hdr->size = CONFIG_EFI_LOG_SIZE;

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
