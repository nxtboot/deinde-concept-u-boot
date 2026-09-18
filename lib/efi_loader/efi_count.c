// SPDX-License-Identifier: GPL-2.0+
/*
 * Counting of calls from an EFI app
 *
 * Copyright 2026 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_loader.h>
#include <stdio.h>

ulong efi_call_count;

void efi_count_show(void)
{
	printf("\nEFI calls: %ld\n", efi_call_count);
}
