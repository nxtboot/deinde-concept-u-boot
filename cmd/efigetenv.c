// SPDX-License-Identifier: GPL-2.0+
#include <charset.h>
#include <command.h>
#include <efi_loader.h>
#include <efi_variable.h>
#include <env.h>
#include <hexdump.h>
#include <malloc.h>
#include <u-boot/uuid.h>

/* Set a U-Boot environment variable to the contents of a UEFI variable */
int do_efi_get_env(struct cmd_tbl *cmdtb, int flat, int argc, char *const argv[])
{
	u16 *var_name = NULL;
	char *strdata = NULL;
	efi_uintn_t size = 0;
	bool var_content_is_utf16_string = false;
	efi_status_t ret;
	efi_guid_t guid;
	u8 *data = NULL;
	u32 attributes;
	size_t len;
	u64 time;
	u16 *p;

	ret = efi_init_obj_list();
	if (ret != EFI_SUCCESS) {
		printf("Error: Cannot initialize UEFI sub-system, r = %lu\n",
		       ret & ~EFI_ERROR_MASK);
		return CMD_RET_FAILURE;
	}

	argv++;
	argc--;

	if (argc != 3 && argc != 4)
		return CMD_RET_USAGE;

	if (argc == 4) {
		if (strcmp(argv[0], "-s"))
			return CMD_RET_USAGE;
		var_content_is_utf16_string = true;
		argv++;
		argc--;
	}

	len = utf8_utf16_strnlen(argv[0], strlen(argv[0]));
	var_name = malloc((len + 1) * 2);
	if (!var_name) {
		printf("## Out of memory\n");
		return CMD_RET_FAILURE;
	}
	p = var_name;
	utf8_utf16_strncpy(&p, argv[0], len + 1);

	if (uuid_str_to_bin(argv[1], guid.b, UUID_STR_FORMAT_GUID)) {
		ret = CMD_RET_USAGE;
		goto out;
	}

	ret = efi_get_variable_int(var_name, &guid, &attributes, &size, data,
				   &time);
	if (ret == EFI_BUFFER_TOO_SMALL) {
		data = malloc(size);
		if (!data) {
			printf("## Out of memory\n");
			ret = CMD_RET_FAILURE;
			goto out;
		}
		ret = efi_get_variable_int(var_name, &guid, &attributes,
					   &size, data, &time);
	}

	if (ret == EFI_NOT_FOUND) {
		printf("Error: \"%ls\" not defined\n", var_name);
		ret = CMD_RET_FAILURE;
		goto out;
	}

	if (ret != EFI_SUCCESS) {
		printf("Error: Cannot read variable, r = %lu\n",
		       ret & ~EFI_ERROR_MASK);
		ret = CMD_RET_FAILURE;
		goto out;
	}

	if (var_content_is_utf16_string) {
		char *p;

		len = utf16_utf8_strnlen((u16 *)data, size / 2);
		strdata = malloc(len + 1);
		if (!strdata) {
			printf("## Out of memory\n");
			ret = CMD_RET_FAILURE;
			goto out;
		}
		p = strdata;
		utf16_utf8_strncpy(&p, (u16 *)data, size / 2);
	} else {
		len = size * 2;
		strdata = malloc(len + 1);
		if (!strdata) {
			printf("## Out of memory\n");
			ret = CMD_RET_FAILURE;
			goto out;
		}
		bin2hex(strdata, data, size);
	}

	strdata[len] = '\0';

	ret = env_set(argv[2], strdata);
	if (ret) {
		ret = CMD_RET_FAILURE;
		goto out;
	}

	ret = CMD_RET_SUCCESS;
out:
	free(strdata);
	free(data);
	free(var_name);

	return ret;
}

U_BOOT_CMD(
	efigetenv, 5, 4, do_efi_get_env,
	"set environment variable to content of EFI variable",
	"[-s] name guid envvar\n"
	"    - set environment variable 'envvar' to the EFI variable 'name'-'guid'\n"
	"      \"-s\": Interpret the EFI variable value as a UTF-16 string\n"
);
