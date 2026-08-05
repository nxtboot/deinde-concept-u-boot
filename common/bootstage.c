// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2011, Google Inc. All rights reserved.
 */

/*
 * This module records the progress of boot and arbitrary commands, and
 * permits accurate timestamping of each.
 */

#define LOG_CATEGORY	LOGC_BOOT

#include <bloblist.h>
#include <bootstage.h>
#include <hang.h>
#include <log.h>
#include <malloc.h>
#include <sort.h>
#include <spl.h>
#include <asm/global_data.h>
#include <linux/compiler.h>
#include <linux/libfdt.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * Result of the last unstash, kept so that it can be shown once the console
 * is up. This is set before relocation, so it must not live in bss
 */
int bootstage_unstash_err __section(".data");

enum {
	RECORD_COUNT = CONFIG_VAL(BOOTSTAGE_RECORD_COUNT),
};

struct bootstage_data {
	uint rec_count;
	uint next_id;
	struct bootstage_record record[RECORD_COUNT];
};

enum {
	BOOTSTAGE_VERSION	= 1,
	BOOTSTAGE_MAGIC		= 0xb00757a3,
	BOOTSTAGE_DIGITS	= 9,
};

struct bootstage_hdr {
	u32 version;		/* BOOTSTAGE_VERSION */
	u32 count;		/* Number of records */
	u32 size;		/* Total data size (non-zero if valid) */
	u32 magic;		/* Magic number */
	u32 next_id;		/* Next ID to use for bootstage */
};

static const char *get_record_name(char *buf, int len,
				   const struct bootstage_record *rec);

int bootstage_relocate(void *to, int size)
{
	struct bootstage_data *data;
	char *ptr, *end;
	char buf[20];
	int i;

	debug("Copying bootstage from %p to %p\n", gd->bootstage, to);
	memcpy(to, gd->bootstage, sizeof(struct bootstage_data));
	data = gd->bootstage = to;

	/* Figure out where to relocate the strings to */
	ptr = (char *)(data + 1);
	end = (char *)to + size;

	/*
	 * Duplicate all strings.  They may point to an old location in the
	 * program .text section that can eventually get trashed.
	 */
	debug("Relocating %d records\n", data->rec_count);
	for (i = 0; i < data->rec_count; i++) {
		const char *from;
		int len;

		from = get_record_name(buf, sizeof(buf), &data->record[i]);
		len = strlen(from) + 1;

		/*
		 * The space was reserved earlier in board_init_f(), so any
		 * record added since then may not fit. Leave the remaining
		 * names where they are rather than writing out of bounds
		 */
		if (ptr + len > end) {
			log_warning("Bootstage: no room to relocate names\n");
			break;
		}
		memcpy(ptr, from, len);
		data->record[i].name = ptr;
		ptr += len;
	}

	return 0;
}

struct bootstage_record *find_id(struct bootstage_data *data,
				 enum bootstage_id id)
{
	struct bootstage_record *rec;
	struct bootstage_record *end;

	for (rec = data->record, end = rec + data->rec_count; rec < end;
	     rec++) {
		if (rec->id == id)
			return rec;
	}

	return NULL;
}

struct bootstage_record *ensure_id(struct bootstage_data *data,
				   enum bootstage_id id)
{
	struct bootstage_record *rec;

	rec = find_id(data, id);
	if (!rec && data->rec_count < RECORD_COUNT) {
		rec = &data->record[data->rec_count++];
		rec->id = id;
		rec->run_cnt = 0;
		return rec;
	}

	return rec;
}

ulong bootstage_add_record(enum bootstage_id id, const char *name,
			   int flags, ulong mark)
{
	struct bootstage_data *data = gd->bootstage;
	struct bootstage_record *rec;

	/*
	 * initf_bootstage() is called very early during boot but since hang()
	 * calls bootstage_error() we can be called before bootstage is set up.
	 * Add a check to avoid this.
	 */
	if (!data)
		return mark;
	if (flags & BOOTSTAGEF_ALLOC)
		id = data->next_id++;

	/* Only record the first event for each */
	rec = find_id(data, id);
	if (!rec) {
		if (data->rec_count < RECORD_COUNT) {
			rec = &data->record[data->rec_count++];
			rec->time_us = mark;
			rec->name = name;
			rec->flags = flags;
			rec->id = id;
			rec->run_cnt = 0;
		} else {
			log_warning("Bootstage space exhausted\n");
		}
	}

	/* Tell the board about this progress */
	show_boot_progress(flags & BOOTSTAGEF_ERROR ? -id : id);

	return mark;
}

ulong bootstage_error_name(enum bootstage_id id, const char *name)
{
	return bootstage_add_record(id, name, BOOTSTAGEF_ERROR,
				    timer_get_boot_us());
}

ulong bootstage_mark_name(enum bootstage_id id, const char *name)
{
	int flags = 0;

	if (id == BOOTSTAGE_ID_ALLOC)
		flags = BOOTSTAGEF_ALLOC;

	return bootstage_add_record(id, name, flags, timer_get_boot_us());
}

ulong bootstage_mark_code(const char *file, const char *func, int linenum)
{
	char *str, *p;
	__maybe_unused char *end;
	int len = 0;

	/* First work out the length we need to allocate */
	if (linenum != -1)
		len = 11;
	if (func)
		len += strlen(func);
	if (file)
		len += strlen(file);

	str = malloc(len + 1);
	p = str;
	end = p + len;
	if (file)
		p += snprintf(p, end - p, "%s,", file);
	if (linenum != -1)
		p += snprintf(p, end - p, "%d", linenum);
	if (func)
		p += snprintf(p, end - p, ": %s", func);

	return bootstage_mark_name(BOOTSTAGE_ID_ALLOC, str);
}

uint32_t bootstage_start(enum bootstage_id id, const char *name)
{
	struct bootstage_data *data = gd->bootstage;
	ulong start_us = timer_get_boot_us();
	struct bootstage_record *rec;

	/*
	 * This can be called before bootstage_init(), for example from code
	 * which runs during early driver-model setup
	 */
	if (!data)
		return start_us;

	rec = ensure_id(data, id);
	if (rec) {
		rec->start_us = start_us;
		rec->name = name;
	}

	return start_us;
}

uint32_t bootstage_accum(enum bootstage_id id)
{
	struct bootstage_data *data = gd->bootstage;
	struct bootstage_record *rec;
	uint32_t duration;

	if (!data)
		return 0;
	rec = ensure_id(data, id);
	if (!rec)
		return 0;
	duration = (uint32_t)timer_get_boot_us() - rec->start_us;
	rec->time_us += duration;
	rec->run_cnt++;

	return duration;
}

uint bootstage_get_rec_count(void)
{
	struct bootstage_data *data = gd->bootstage;

	if (!data)
		return 0;

	return data->rec_count;
}

const struct bootstage_record *bootstage_get_rec(uint index)
{
	struct bootstage_data *data = gd->bootstage;

	if (!data || index >= data->rec_count)
		return NULL;

	return &data->record[index];
}

void bootstage_set_rec_count(uint count)
{
	struct bootstage_data *data = gd->bootstage;
	uint i;

	if (!data || count > RECORD_COUNT)
		return;

	/* Clear any records beyond the new count */
	for (i = count; i < data->rec_count; i++) {
		data->record[i].time_us = 0;
		data->record[i].start_us = 0;
	}

	data->rec_count = count;
}

ulong bootstage_get_time(enum bootstage_id id)
{
	struct bootstage_data *data = gd->bootstage;
	struct bootstage_record *rec;

	if (!data)
		return 0;
	rec = find_id(data, id);
	if (!rec)
		return 0;

	return rec->time_us;
}

/**
 * Get a record name as a printable string
 *
 * @param buf	Buffer to put name if needed
 * @param len	Length of buffer
 * @param rec	Boot stage record to get the name from
 * Return: pointer to name, either from the record or pointing to buf.
 */
static const char *get_record_name(char *buf, int len,
				   const struct bootstage_record *rec)
{
	if (rec->name)
		return rec->name;
	else if (rec->id >= BOOTSTAGE_ID_USER)
		snprintf(buf, len, "user_%d", rec->id - BOOTSTAGE_ID_USER);
	else
		snprintf(buf, len, "id=%d", rec->id);

	return buf;
}

static uint32_t print_time_record(struct bootstage_record *rec, uint32_t prev)
{
	char buf[20];

	if (prev == -1U) {
		printf("%11s", "");
		print_grouped_ull(rec->time_us, BOOTSTAGE_DIGITS);
	} else {
		if (prev > rec->time_us)
			prev = 0;
		print_grouped_ull(rec->time_us, BOOTSTAGE_DIGITS);
		print_grouped_ull(rec->time_us - prev, BOOTSTAGE_DIGITS);
	}
	printf("  %s", get_record_name(buf, sizeof(buf), rec));

	/*
	 * For an accumulator, show how often it ran and the average cost of
	 * each call, which is what indicates whether it is worth optimising
	 */
	if (prev == -1U && rec->run_cnt)
		printf(" (%u call%s, %lu us each)", rec->run_cnt,
		       rec->run_cnt == 1 ? "" : "s",
		       rec->time_us / rec->run_cnt);
	printf("\n");

	return rec->time_us;
}

#ifdef CONFIG_OF_LIBFDT
/**
 * Add all bootstage timings to a device tree.
 *
 * @param blob	Device tree blob
 * Return: 0 on success, != 0 on failure.
 */
static int add_bootstages_devicetree(struct fdt_header *blob)
{
	struct bootstage_data *data = gd->bootstage;
	int bootstage;
	char buf[20];
	int recnum;
	int i;

	if (!blob)
		return 0;

	/*
	 * Create the node for bootstage.
	 * The address of flat device tree is set up by the command bootm.
	 */
	bootstage = fdt_add_subnode(blob, 0, "bootstage");
	if (bootstage < 0)
		return -EINVAL;

	/*
	 * Insert the timings to the device tree in the reverse order so
	 * that they can be printed in the Linux kernel in the right order.
	 */
	for (recnum = data->rec_count - 1, i = 0; recnum >= 0; recnum--, i++) {
		struct bootstage_record *rec = &data->record[recnum];
		int node;

		if (rec->id != BOOTSTAGE_ID_AWAKE && rec->time_us == 0)
			continue;

		node = fdt_add_subnode(blob, bootstage, simple_itoa(i));
		if (node < 0)
			break;

		/* add properties to the node. */
		if (fdt_setprop_string(blob, node, "name",
				       get_record_name(buf, sizeof(buf), rec)))
			return -EINVAL;

		/* Check if this is a 'mark' or 'accum' record */
		if (fdt_setprop_cell(blob, node,
				rec->start_us ? "accum" : "mark",
				rec->time_us))
			return -EINVAL;
	}

	return 0;
}

int bootstage_fdt_add_report(void)
{
	if (add_bootstages_devicetree(working_fdt))
		puts("bootstage: Failed to add to device tree\n");

	return 0;
}
#endif

void bootstage_report(void)
{
	struct bootstage_data *data = gd->bootstage;
	struct bootstage_record *rec = data->record;
	uint32_t prev;
	int i;

	printf("Timer summary in microseconds (%d records):\n",
	       data->rec_count);
	printf("%11s%11s  %s\n", "Mark", "Elapsed", "Stage");

	prev = print_time_record(rec, 0);

	for (i = 1, rec++; i < data->rec_count; i++, rec++) {
		if (rec->id && !rec->start_us)
			prev = print_time_record(rec, prev);
	}
	if (data->rec_count > RECORD_COUNT)
		printf("Overflowed internal boot id table by %d entries\n"
		       "Please increase CONFIG_(PHASE_)BOOTSTAGE_RECORD_COUNT\n",
		       data->rec_count - RECORD_COUNT);

	if (IS_ENABLED(CONFIG_BOOTSTAGE_STASH) && bootstage_unstash_err)
		printf("Unstash failed: err=%d\n", bootstage_unstash_err);

	puts("\nAccumulated time:\n");
	for (i = 0, rec = data->record; i < data->rec_count; i++, rec++) {
		if (rec->start_us)
			prev = print_time_record(rec, -1);
	}

	if (IS_ENABLED(CONFIG_BOOTSTAGE_ACCUM_DT))
		fdt_parent_report();
}

/**
 * Append data to a memory buffer
 *
 * Write data to the buffer if there is space. Whether there is space or not,
 * the buffer pointer is incremented.
 *
 * @param ptrp	Pointer to buffer, updated by this function
 * @param end	Pointer to end of buffer
 * @param data	Data to write to buffer
 * @param size	Size of data
 */
static void append_data(char **ptrp, char *end, const void *data, int size)
{
	char *ptr = *ptrp;

	*ptrp += size;
	if (*ptrp > end)
		return;

	memcpy(ptr, data, size);
}

int bootstage_stash(void *base, int size)
{
	const struct bootstage_data *data = gd->bootstage;
	struct bootstage_hdr *hdr = (struct bootstage_hdr *)base;
	const struct bootstage_record *rec;
	char buf[20];
	char *ptr = base, *end = ptr + size;
	int i;

	if (hdr + 1 > (struct bootstage_hdr *)end) {
		debug("%s: Not enough space for bootstage hdr\n", __func__);
		return -ENOSPC;
	}

	/* Write an arbitrary version number */
	hdr->version = BOOTSTAGE_VERSION;

	hdr->count = data->rec_count;
	hdr->size = 0;
	hdr->magic = BOOTSTAGE_MAGIC;
	hdr->next_id = data->next_id;
	ptr += sizeof(*hdr);

	/* Write the records, silently stopping when we run out of space */
	for (rec = data->record, i = 0; i < data->rec_count; i++, rec++)
		append_data(&ptr, end, rec, sizeof(*rec));

	/* Write the name strings */
	for (rec = data->record, i = 0; i < data->rec_count; i++, rec++) {
		const char *name;

		name = get_record_name(buf, sizeof(buf), rec);
		append_data(&ptr, end, name, strlen(name) + 1);
	}

	/* Check for buffer overflow */
	if (ptr > end) {
		debug("%s: Not enough space for bootstage stash\n", __func__);
		return -ENOSPC;
	}

	/* Update total data size */
	hdr->size = ptr - (char *)base;
	debug("Stashed %d records\n", hdr->count);

	return 0;
}

int bootstage_unstash(const void *base, int size)
{
	const struct bootstage_hdr *hdr = (struct bootstage_hdr *)base;
	struct bootstage_data *data = gd->bootstage;
	const char *ptr = base, *end = ptr + size;
	struct bootstage_record *rec;
	uint rec_size;
	int i;

	if (size == -1)
		end = (char *)(~(uintptr_t)0);

	if (hdr + 1 > (struct bootstage_hdr *)end) {
		debug("%s: Not enough space for bootstage hdr\n", __func__);
		return -EPERM;
	}

	if (hdr->magic != BOOTSTAGE_MAGIC) {
		debug("%s: Invalid bootstage magic\n", __func__);
		return -ENOENT;
	}

	if (ptr + hdr->size > end) {
		debug("%s: Bootstage data runs past buffer end\n", __func__);
		return -ENOSPC;
	}

	if (hdr->count * sizeof(*rec) > hdr->size) {
		debug("%s: Bootstage has %d records needing %lu bytes, but "
			"only %d bytes is available\n", __func__, hdr->count,
		      (ulong)hdr->count * sizeof(*rec), hdr->size);
		return -ENOSPC;
	}

	if (hdr->version != BOOTSTAGE_VERSION) {
		debug("%s: Bootstage data version %#0x unrecognised\n",
		      __func__, hdr->version);
		return -EINVAL;
	}

	if (data->rec_count + hdr->count > RECORD_COUNT) {
		debug("%s: Bootstage has %d records, we have space for %d\n"
			"Please increase CONFIG_(PHASE_)BOOTSTAGE_RECORD_COUNT\n",
		      __func__, hdr->count, RECORD_COUNT - data->rec_count);
		return -ENOSPC;
	}

	ptr += sizeof(*hdr);

	/* Read the records */
	rec_size = hdr->count * sizeof(*data->record);
	memcpy(data->record + data->rec_count, ptr, rec_size);

	/* Read the name strings */
	ptr += rec_size;
	for (rec = data->record + data->rec_count, i = 0; i < hdr->count;
	     i++, rec++) {
		rec->name = ptr;
		if (xpl_phase() == PHASE_SPL)
			rec->name = strdup(ptr);

		/* Assume no data corruption here */
		ptr += strlen(ptr) + 1;
	}

	/* Mark the records as read */
	data->rec_count += hdr->count;
	data->next_id = hdr->next_id;
	debug("Unstashed %d records\n", hdr->count);

	return 0;
}

#if IS_ENABLED(CONFIG_BOOTSTAGE_STASH)
int _bootstage_stash_default(void)
{
	void *stash;

	stash = NULL;
	if (IS_ENABLED(CONFIG_BOOTSTAGE_STASH_BLOBLIST) &&
	    CONFIG_IS_ENABLED(BLOBLIST)) {
		stash = bloblist_ensure(BLOBLISTT_U_BOOT_BOOTSTAGE,
					CONFIG_BOOTSTAGE_STASH_SIZE);
		/*
		 * An early phase may run before the bloblist exists, e.g.
		 * before memory is available. Fall back to the fixed address
		 * so that its records are not simply dropped
		 */
		if (!stash)
			log_warning("Bootstage: no bloblist, using stash addr\n");
	}
	if (!stash)
		stash = map_sysmem(CONFIG_BOOTSTAGE_STASH_ADDR, 0);

	return bootstage_stash(stash, CONFIG_BOOTSTAGE_STASH_SIZE);
}

int _bootstage_unstash_default(void)
{
	const void *stash;

	stash = NULL;
	if (IS_ENABLED(CONFIG_BOOTSTAGE_STASH_BLOBLIST) &&
	    CONFIG_IS_ENABLED(BLOBLIST))
		stash = bloblist_find(BLOBLISTT_U_BOOT_BOOTSTAGE,
				      CONFIG_BOOTSTAGE_STASH_SIZE);
	/*
	 * The records may come from a phase which ran before the bloblist
	 * existed and so used the fixed address instead
	 */
	if (!stash)
		stash = map_sysmem(CONFIG_BOOTSTAGE_STASH_ADDR,
				   CONFIG_BOOTSTAGE_STASH_SIZE);

	bootstage_unstash_err = bootstage_unstash(stash,
						 CONFIG_BOOTSTAGE_STASH_SIZE);

	return bootstage_unstash_err;
}
#endif

int bootstage_get_size(bool add_strings)
{
	int size;

	size = sizeof(struct bootstage_data);
	if (add_strings) {
		struct bootstage_data *data = gd->bootstage;
		struct bootstage_record *rec;
		char buf[20];
		int i;

		/*
		 * Use the same name as the other users, since a record made by
		 * an accumulator has none of its own
		 */
		for (rec = data->record, i = 0; i < data->rec_count; i++, rec++)
			size += strlen(get_record_name(buf, sizeof(buf), rec)) +
				1;
	}

	return size;
}

int bootstage_init(bool first)
{
	struct bootstage_data *data;
	int size = sizeof(struct bootstage_data);

	gd->bootstage = (struct bootstage_data *)malloc(size);
	if (!gd->bootstage)
		return -ENOMEM;
	data = gd->bootstage;
	memset(data, '\0', size);
	if (first) {
		data->next_id = BOOTSTAGE_ID_USER;
		bootstage_add_record(BOOTSTAGE_ID_AWAKE, "reset", 0, 0);
	}

	return 0;
}
