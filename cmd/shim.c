// SPDX-License-Identifier: GPL-2.0+
/*
 * Control of settings for Shim
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#include <abuf.h>
#include <command.h>
#include <efi_loader.h>
#include <efi_variable.h>
#include <errno.h>
#include <shim.h>
#include <vsprintf.h>

static int do_shim_debug(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	efi_status_t eret;
	struct abuf buf;
	const char *sub;
	u32 value;
	u32 attr;
	int ret;

	/* the variable needs the EFI subsystem, which may not be up yet */
	eret = efi_init_obj_list();
	if (eret != EFI_SUCCESS) {
		printf("Cannot initialise UEFI (err=%lx)\n", eret);
		return CMD_RET_FAILURE;
	}

	sub = cmd_arg1(argc, argv);
	if (argc == 1) {
		ret = efi_read_var(SHIM_VERBOSE_VAR_NAME, &efi_shim_lock, &attr,
				   &buf, NULL);
		if (ret == -ENOENT) {
			value = 0;
		} else if (ret) {
			printf("Failed to read variable (err=%dE)\n", ret);
			goto fail;
		} else if (buf.size != sizeof(value)) {
			printf("Invalid value size %zd\n", buf.size);
			goto fail;
		} else {
			value = *(typeof(value) *)buf.data;
		}
		printf("%d\n", value);
	} else {
		int arg = 1;

		attr = EFI_VARIABLE_BOOTSERVICE_ACCESS;
		if (!strcmp("-n", argv[arg])) {
			attr |= EFI_VARIABLE_NON_VOLATILE;
			arg++;
		}
		if (arg == argc)
			return CMD_RET_USAGE;
		value = hextoul(argv[arg], NULL) ? 1 : 0;
		eret = efi_set_variable_int(SHIM_VERBOSE_VAR_NAME,
					    &efi_shim_lock, attr,
					    sizeof(value), &value, false);
		if (eret) {
			printf("Failed to write variable (err=%lx)\n", eret);
			goto fail;
		}
	}

	return 0;

fail:
	return 1;
}

U_BOOT_LONGHELP(shim,
	"debug [[-n] <0/1>]  - Enable / disable debug verbose mode");

U_BOOT_CMD_WITH_SUBCMDS(shim, "Shim utilities", shim_help_text,
	U_BOOT_SUBCMD_MKENT(debug, 3, 1, do_shim_debug));
