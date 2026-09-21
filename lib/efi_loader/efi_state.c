// SPDX-License-Identifier: GPL-2.0+
/*
 * State of the EFI subsystem
 *
 * Copyright 2026 Google LLC
 */

#include <efi_loader.h>
#include <string.h>
#include <dm/uclass-id.h>

/*
 * The state used from boot; a test can select another with efi_state_set().
 * The OS keeps a pointer to the system table in here, so it is runtime data.
 */
static struct efi_state __efi_runtime_data efi_default_state;

struct efi_state *efis __efi_runtime_data = &efi_default_state;

void efi_state_init(struct efi_state *st)
{
	memset(st, '\0', sizeof(*st));
	efi_console_init_state(&st->con);
	efi_bs_init_state(&st->bs);
	efi_systab_init_state(&st->systab);
	st->obj_list_initialized = EFI_OBJ_LIST_NOT_INIT;
	efi_mem_init_state(&st->mem);
	INIT_LIST_HEAD(&st->hii.package_lists);
	INIT_LIST_HEAD(&st->hii.keyboard_layouts);
	INIT_LIST_HEAD(&st->rt.mmio);
	st->system_partition.uclass_id = UCLASS_INVALID;
}

int efi_state_init_default(void)
{
	efi_state_init(efis);

	return 0;
}

int efi_state_start(void)
{
	int ret;

	/* LMB told the default state about memory as U-Boot started */
	ret = efi_memory_add_lmb();
	if (ret)
		return ret;
	ret = efi_memory_init();
	if (ret)
		return ret;

	/* the rest of efi_init_early() is for U-Boot as a whole */
	return efi_init_early_state();
}

struct efi_state *efi_state_set(struct efi_state *st)
{
	struct efi_state *old = efis;

	efis = st;

	return old;
}

void efi_state_uninit(void)
{
	/* this needs the interface objects, which are handles */
	if (IS_ENABLED(CONFIG_NETDEVICES))
		efi_net_uninit_state(&efis->net);
	efi_console_uninit_state(&efis->con);
	efi_driver_uninit();
	efi_bs_uninit_state(&efis->bs);
	efi_mem_uninit_state(&efis->mem);
	efis->obj_list_initialized = EFI_OBJ_LIST_NOT_INIT;
}
