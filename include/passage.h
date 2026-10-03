/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Standard passage implementation
 *
 * Copyright 2022 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#ifndef __PASSAGE_H
#define __PASSAGE_H

#include <stdbool.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

enum {
	PASSAGE_ABI_MACH	= 0x4a0fb10bul,
	PASSAGE_ABI_VERSION	= 1ul,
};

static inline ulong passage_mach_version(void)
{
#if BITS_PER_LONG == 64
	return PASSAGE_ABI_MACH | (ulong)PASSAGE_ABI_VERSION << 32;
#else
	return (PASSAGE_ABI_MACH & 0xffffff) | (PASSAGE_ABI_VERSION << 24);
#endif
}

/**
 * passage_valid() - See if standard passage was provided by the previous phase
 *
 * Return: true if standard passage was provided, else false
 */
static inline bool passage_valid(void)
{
#if IS_ENABLED(CONFIG_BLOBLIST_PASSAGE)
	return gd->passage_mach == passage_mach_version();
#else
	return false;
#endif
}

/* arch_passage_entry() - Jump to the next phase, using standard passage
 *
 * @entry_addr: Address to jump to
 * @bloblist: Bloblist address to pass
 * @fdt: FDT to pass
 */
void __noreturn arch_passage_entry(ulong entry_addr, ulong bloblist, ulong fdt);

#endif
