// SPDX-License-Identifier: GPL-2.0+
/*
 * Access to the APOB on AMD EPYC Turin
 *
 * The PSP's ABL records what it found and did (the memory map, the DIMMs
 * and their SPDs, its event log and so on) in the APOB, a list of entries
 * it leaves in DRAM for the BIOS to read.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <asm/arch/apob.h>

const struct apob_entry *turin_apob_find(uint group, uint type, uint instance)
{
	const struct apob_header *apob = (void *)APOB_BASE;
	ulong pos, end;

	if (apob->signature != APOB_SIGNATURE)
		return NULL;
	end = APOB_BASE + apob->size;
	for (pos = APOB_BASE + apob->first_entry; pos < end;) {
		const struct apob_entry *entry = (void *)pos;

		if (!entry->size)
			break;
		if (entry->group == group && entry->type == type &&
		    entry->instance == instance)
			return entry;
		pos += entry->size;
	}

	return NULL;
}
