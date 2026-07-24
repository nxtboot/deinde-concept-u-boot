// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_stub.h>
#include <errno.h>
#include <log.h>
#include <malloc.h>
#include <u-boot/uuid.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

static bool is_app(void)
{
	if (!IS_ENABLED(CONFIG_EFI_APP)) {
		printf("This command is only available in the app\n");
		return false;
	}

	return true;
}

static int do_efi_image(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	struct efi_loaded_image *lim;
	struct efi_priv *priv;
	u16 *path;

	if (!is_app())
		return CMD_RET_FAILURE;

	priv = efi_get_priv();
	lim = priv->loaded_image;
	path = efi_dp_str(lim->file_path);
	printf("Loaded-image path: %ls\n", path);

	return 0;
}

static int do_efi_mem(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	struct efi_mem_desc *orig;
	uint version, key = 0;
	int desc_size;
	int size, ret;

	if (IS_ENABLED(CONFIG_EFI_APP)) {
		ret = efi_get_mmap(&orig, &size, &key, &desc_size, &version);
		if (ret) {
			printf("Cannot read memory map (err=%d)\n", ret);
			return CMD_RET_FAILURE;
		}
	} else {
		struct efi_entry_memmap *map;

		ret = efi_info_get(EFIET_MEMORY_MAP, (void **)&map, &size);
		switch (ret) {
		case -ENOENT:
			printf("No EFI table available\n");
			goto done;
		case -EPROTONOSUPPORT:
			printf("Incorrect EFI table version\n");
			goto done;
		}
		orig = map->desc;
		desc_size = map->desc_size;
		version = map->version;
	}
	printf("EFI table at %lx, memory map %p, size %x, key %x, version %x, descr. size %#x\n",
	       gd->arch.table, orig, size, key, version, desc_size);
	if (version != EFI_MEM_DESC_VERSION) {
		printf("Incorrect memory map version\n");
		ret = -EPROTONOSUPPORT;
		goto done;
	}

	efi_show_memmap(orig, size, desc_size);
	if (IS_ENABLED(CONFIG_EFI_APP))
		free(orig);
done:
	if (ret)
		printf("Error: %d\n", ret);

	return ret ? CMD_RET_FAILURE : 0;
}

static int do_efi_tables(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	struct efi_system_table *systab;

	if (IS_ENABLED(CONFIG_EFI_APP)) {
		systab = efi_get_sys_table();
		if (!systab) {
			printf("Cannot read system table\n");
			return CMD_RET_FAILURE;
		}
	} else {
		int size;
		int ret;

		ret = efi_info_get(EFIET_SYS_TABLE, (void **)&systab, &size);
		if (ret)  /* this should not happen */
			return CMD_RET_FAILURE;
	}

	efi_show_tables(systab);

	return 0;
}

static int do_efi_memsync(struct cmd_tbl *cmdtp, int flag, int argc,
			  char *const argv[])
{
	bool verbose = false;
	int ret;

	if (!is_app() || IS_ENABLED(CONFIG_X86))
		return CMD_RET_FAILURE;
	if (argc > 1 && !strcmp(argv[1], "-v"))
		verbose = true;
	if (!working_fdt) {
		printf("No working FDT set\n");
		return CMD_RET_FAILURE;
	}

	ret = efi_mem_reserved_sync(working_fdt, verbose);
	if (ret < 0)
		return CMD_RET_FAILURE;

	return 0;
}

static struct cmd_tbl efi_commands[] = {
	U_BOOT_CMD_MKENT(image, 1, 1, do_efi_image, "", ""),
	U_BOOT_CMD_MKENT(mem, 0, 1, do_efi_mem, "", ""),
	U_BOOT_CMD_MKENT(memsync, 2, 1, do_efi_memsync, "", ""),
	U_BOOT_CMD_MKENT(tables, 1, 1, do_efi_tables, "", ""),
};

static int do_efi(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	struct cmd_tbl *efi_cmd;
	int ret;

	if (argc < 2)
		return CMD_RET_USAGE;
	efi_cmd = find_cmd_tbl(argv[1], efi_commands, ARRAY_SIZE(efi_commands));
	argc -= 2;
	argv += 2;
	if (!efi_cmd || argc > efi_cmd->maxargs)
		return CMD_RET_USAGE;

	ret = efi_cmd->cmd(efi_cmd, flag, argc, argv);

	return cmd_process_error(efi_cmd, ret);
}

U_BOOT_CMD(
	efi,     4,      1,      do_efi,
	"EFI access",
	"image            Dump loaded-image info\n"
	"mem              Dump memory map\n"
	"memsync [-v]     Sync EFI memory map with DT reserved-memory\n"
	"tables           Dump tables"
);
