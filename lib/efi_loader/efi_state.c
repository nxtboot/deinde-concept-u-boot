// SPDX-License-Identifier: GPL-2.0+
/*
 * State of the EFI subsystem
 *
 * Copyright 2026 Google LLC
 */

#include <efi_loader.h>
#include <string.h>

/* The state used from boot; a test can select another with efi_state_set() */
static struct efi_state efi_default_state;

struct efi_state *efis = &efi_default_state;

void efi_state_init(struct efi_state *st)
{
	memset(st, '\0', sizeof(*st));
}

int efi_state_init_default(void)
{
	efi_state_init(efis);

	return 0;
}

struct efi_state *efi_state_set(struct efi_state *st)
{
	struct efi_state *old = efis;

	efis = st;

	return old;
}

void efi_state_uninit(void)
{
}
