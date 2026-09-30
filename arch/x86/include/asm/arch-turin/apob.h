/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * The AGESA PSP Output Block (APOB) which the ABL leaves in DRAM
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef __ASM_ARCH_APOB_H
#define __ASM_ARCH_APOB_H

#include <linux/types.h>

/* Where the ABL puts the APOB, as set in the APCB */
#define APOB_BASE		0x7010000
#define APOB_SIGNATURE		0x424f5041	/* "APOB" */
#define APOB_HMAC_SIZE		32

/* Groups of entries */
enum apob_group {
	APOB_GROUP_MEM = 1,
	APOB_GROUP_SMBIOS = 8,
	APOB_GROUP_FABRIC = 9,
};

/* Types of entry within the groups used here */
#define APOB_TYPE_MEM_SPD_DATA	17	/* APOB_GROUP_MEM: the DIMMs' SPDs */
#define APOB_TYPE_MEM_SMBIOS	8	/* APOB_GROUP_SMBIOS: DIMM summary */
#define APOB_TYPE_SYS_MAP	9	/* APOB_GROUP_FABRIC: memory map */

/**
 * struct apob_header - Header at the start of the APOB
 *
 * @signature: APOB_SIGNATURE ("APOB")
 * @version: Version of the APOB's layout
 * @size: Size of the whole APOB in bytes, including this header
 * @first_entry: Offset of the first entry from the start of the APOB
 */
struct apob_header {
	u32 signature;
	u32 version;
	u32 size;
	u32 first_entry;
};

/**
 * struct apob_entry - Header of each entry in the APOB
 *
 * The entry's data follows the header. The entries are grouped by the part
 * of the ABL which writes them, such as its memory or fabric code, and each
 * group numbers its own types.
 *
 * @group: Group the entry is in
 * @type: Type of entry within the group
 * @instance: Instance of the type, for entries which have several, such as
 *	one per die
 * @size: Size of the entry in bytes, including this header
 * @hmac: HMAC of the entry, which U-Boot does not check
 */
struct apob_entry {
	u32 group;
	u32 type;
	u32 instance;
	u32 size;
	u8 hmac[APOB_HMAC_SIZE];
};

/**
 * turin_apob_find() - Find an entry in the APOB
 *
 * @group: Group the entry is in (enum apob_group)
 * @type: Type of entry within the group
 * @instance: Instance number, for entries which have several
 * Return: pointer to the entry's header, which its data follows, or NULL if
 *	there is no APOB or no such entry
 */
const struct apob_entry *turin_apob_find(uint group, uint type, uint instance);

#endif
