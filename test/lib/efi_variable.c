// SPDX-License-Identifier: GPL-2.0+
/*
 * Test EFI variable store functions
 *
 * Copyright 2026 Canonical Ltd.
 */

#include <charset.h>
#include <efi_loader.h>
#include <efi_variable.h>
#include <malloc.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>
#include <u-boot/crc.h>

static const efi_guid_t test_guid =
	EFI_GUID(0x8a7b3ce0, 0x6f25, 0x4a2d, 0x9c, 0x11,
		 0x2e, 0x5d, 0x71, 0x3b, 0x9f, 0x40);

#define ATTR_NV	(EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | \
		 EFI_VARIABLE_RUNTIME_ACCESS)
#define ATTR_VOL (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)

/**
 * add_var() - Append a variable to a store held in a buffer
 *
 * @buf: Store to append to
 * @name: Variable name
 * @attr: Attributes
 * @data: Value
 * @size: Size of value in bytes
 */
static void add_var(struct efi_var_file *buf, const u16 *name, u32 attr,
		    const void *data, uint size)
{
	struct efi_var_entry *var = (void *)((u8 *)buf + buf->length);
	u16 *ptr;

	var->length = size;
	var->attr = attr;
	var->time = 0;
	memcpy(&var->guid, &test_guid, sizeof(test_guid));
	ptr = var->name;
	u16_strcpy(ptr, name);
	ptr += u16_strlen(name) + 1;
	memcpy(ptr, data, size);
	buf->length = ALIGN((uintptr_t)ptr + size, 8) - (uintptr_t)buf;
	buf->crc32 = crc32(0, (u8 *)buf->var,
			   buf->length - sizeof(struct efi_var_file));
}

/**
 * get_var() - Read a test variable into a string buffer
 *
 * @uts: Test state
 * @name: Variable name
 * @str: Buffer for the value, terminated with \0
 * @size: Size of buffer
 * Return: EFI status
 */
static efi_status_t get_var(struct unit_test_state *uts, const u16 *name,
			    char *str, efi_uintn_t size)
{
	efi_status_t ret;

	memset(str, '\0', size);
	size--;
	ret = efi_get_variable_int(name, &test_guid, NULL, &size, str, NULL);

	return ret;
}

/* Test applying a store which survived a warm reset */
static int lib_test_efi_var_apply(struct unit_test_state *uts)
{
	struct efi_var_file *buf;
	char str[16];

	if (!IS_ENABLED(CONFIG_EFI_RT_VOLATILE_STORE))
		return -EAGAIN;
	ut_asserteq(EFI_SUCCESS, efi_init_obj_list());

	/* One variable the OS changed, one it deleted, one it left alone */
	ut_asserteq(EFI_SUCCESS, efi_set_variable_int(u"changed", &test_guid,
						      ATTR_NV, 4, "old", false));
	ut_asserteq(EFI_SUCCESS, efi_set_variable_int(u"deleted", &test_guid,
						      ATTR_NV, 2, "x", false));
	ut_asserteq(EFI_SUCCESS, efi_set_variable_int(u"volatile", &test_guid,
						      ATTR_VOL, 5, "keep", false));

	buf = calloc(1, EFI_VAR_BUF_SIZE);
	ut_assertnonnull(buf);
	buf->magic = EFI_VAR_FILE_MAGIC;
	buf->length = sizeof(*buf);
	add_var(buf, u"changed", ATTR_NV, "new", 4);
	add_var(buf, u"added", ATTR_NV, "yes", 4);
	/* a volatile variable in the buffer must be ignored */
	add_var(buf, u"stale", ATTR_VOL, "no", 3);

	ut_asserteq(EFI_SUCCESS, efi_var_apply(buf));
	free(buf);

	ut_asserteq(EFI_SUCCESS, get_var(uts, u"changed", str, sizeof(str)));
	ut_asserteq_str("new", str);
	ut_asserteq(EFI_SUCCESS, get_var(uts, u"added", str, sizeof(str)));
	ut_asserteq_str("yes", str);
	ut_asserteq_64(EFI_NOT_FOUND, get_var(uts, u"deleted", str, sizeof(str)));
	ut_asserteq_64(EFI_NOT_FOUND, get_var(uts, u"stale", str, sizeof(str)));
	ut_asserteq(EFI_SUCCESS, get_var(uts, u"volatile", str, sizeof(str)));
	ut_asserteq_str("keep", str);

	/* A corrupt buffer is rejected */
	buf = calloc(1, sizeof(*buf) + 64);
	ut_assertnonnull(buf);
	buf->magic = EFI_VAR_FILE_MAGIC;
	buf->length = sizeof(*buf);
	add_var(buf, u"bad", ATTR_NV, "x", 2);
	buf->crc32 ^= 1;
	ut_asserteq_64(EFI_INVALID_PARAMETER, efi_var_apply(buf));
	free(buf);

	/* Clean up */
	efi_set_variable_int(u"changed", &test_guid, 0, 0, NULL, false);
	efi_set_variable_int(u"added", &test_guid, 0, 0, NULL, false);
	efi_set_variable_int(u"volatile", &test_guid, 0, 0, NULL, false);

	return 0;
}
LIB_TEST(lib_test_efi_var_apply, 0);
