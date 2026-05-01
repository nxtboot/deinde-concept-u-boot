// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <bootm.h>
#include <efi.h>
#include <event.h>
#include <init.h>

int print_cpuinfo(void)
{
	return 0;
}

int board_init(void)
{
	return 0;
}

int board_exit_boot_services(void *ctx, struct event *evt)
{
	struct efi_priv *priv = efi_get_priv();
	struct efi_mem_desc *desc;
	int desc_size;
	uint version;
	int size;
	uint key;
	int ret;

	if (evt->data.bootm_final.flag & BOOTM_STATE_OS_FAKE_GO) {
		printf("Not exiting EFI (fake go)\n");
		return 0;
	}
	printf("Exiting EFI\n");
	ret = efi_get_mmap(&desc, &size, &key, &desc_size, &version);
	if (ret) {
		printf("efi: Failed to get memory map\n");
		return -EFAULT;
	}

	ret = efi_app_exit_boot_services(priv, key);
	if (ret)
		return ret;

	/* no console output after here as there are no EFI drivers! */

	return 0;
}
EVENT_SPY_FULL(EVT_BOOTM_FINAL, board_exit_boot_services);
