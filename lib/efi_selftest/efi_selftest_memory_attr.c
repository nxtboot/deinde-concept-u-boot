// SPDX-License-Identifier: GPL-2.0+
/*
 * efi_selftest_memory_attr
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * This unit test checks the EFI_MEMORY_ATTRIBUTE_PROTOCOL, following the calls
 * which shim makes for an image it loads: the whole image is made
 * non-executable, then each code section is made read-only and executable
 * again by clearing RP and XP together.
 */

#include <efi_selftest.h>

#define NUM_PAGES	2

static struct efi_boot_services *boottime;
static struct efi_memory_attribute_protocol *mattr;
static efi_physical_addr_t pages;

static const efi_guid_t guid_memory_attribute =
	EFI_MEMORY_ATTRIBUTE_PROTOCOL_GUID;

/**
 * setup() - Find the protocol and allocate some pages to change
 *
 * @handle:	handle of the loaded image
 * @systable:	system table
 * Return:	EFI_ST_SUCCESS for success
 */
static int setup(const efi_handle_t handle,
		 const struct efi_system_table *systable)
{
	efi_status_t ret;

	boottime = systable->boottime;
	ret = boottime->locate_protocol(&guid_memory_attribute, NULL,
					(void **)&mattr);
	if (ret != EFI_SUCCESS) {
		efi_st_error("EFI_MEMORY_ATTRIBUTE_PROTOCOL not found\n");
		return EFI_ST_FAILURE;
	}
	ret = boottime->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_CODE,
				       NUM_PAGES, &pages);
	if (ret != EFI_SUCCESS) {
		efi_st_error("AllocatePages failed\n");
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}

/**
 * teardown() - Make the pages writable and executable again, then free them
 *
 * Return:	EFI_ST_SUCCESS for success
 */
static int teardown(void)
{
	efi_status_t ret;

	if (!pages)
		return EFI_ST_SUCCESS;
	ret = mattr->clear_memory_attributes(mattr, pages,
					     NUM_PAGES * EFI_PAGE_SIZE,
					     EFI_MEMORY_RO | EFI_MEMORY_XP);
	if (ret != EFI_SUCCESS) {
		efi_st_error("Cannot restore the attributes\n");
		return EFI_ST_FAILURE;
	}
	ret = boottime->free_pages(pages, NUM_PAGES);
	if (ret != EFI_SUCCESS) {
		efi_st_error("FreePages failed\n");
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}

/**
 * check_attrs() - Check the attributes of one page
 *
 * @addr:	address of the page
 * @expect:	attributes expected
 * Return:	EFI_ST_SUCCESS for success
 */
static int check_attrs(efi_physical_addr_t addr, u64 expect)
{
	efi_status_t ret;
	u64 attrs;

	ret = mattr->get_memory_attributes(mattr, addr, EFI_PAGE_SIZE, &attrs);
	if (ret != EFI_SUCCESS) {
		efi_st_error("GetMemoryAttributes failed\n");
		return EFI_ST_FAILURE;
	}
	if (attrs != expect) {
		efi_st_error("Attributes %llx, expected %llx\n", attrs, expect);
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}

/**
 * execute() - Change the attributes as shim does and check the results
 *
 * Return:	EFI_ST_SUCCESS for success
 */
static int execute(void)
{
	efi_physical_addr_t code = pages;
	efi_physical_addr_t data = pages + EFI_PAGE_SIZE;
	u64 size = NUM_PAGES * EFI_PAGE_SIZE;
	efi_status_t ret;

	/* The whole image is made writable but not executable */
	ret = mattr->set_memory_attributes(mattr, pages, size, EFI_MEMORY_XP);
	if (ret != EFI_SUCCESS) {
		efi_st_error("Cannot set XP\n");
		return EFI_ST_FAILURE;
	}
	ret = mattr->clear_memory_attributes(mattr, pages, size,
					     EFI_MEMORY_RP | EFI_MEMORY_RO);
	if (ret != EFI_SUCCESS) {
		efi_st_error("Cannot clear RP and RO\n");
		return EFI_ST_FAILURE;
	}
	if (check_attrs(code, EFI_MEMORY_XP) || check_attrs(data, EFI_MEMORY_XP))
		return EFI_ST_FAILURE;

	/* A code section is made read-only and executable */
	ret = mattr->set_memory_attributes(mattr, code, EFI_PAGE_SIZE,
					   EFI_MEMORY_RO);
	if (ret != EFI_SUCCESS) {
		efi_st_error("Cannot set RO\n");
		return EFI_ST_FAILURE;
	}
	ret = mattr->clear_memory_attributes(mattr, code, EFI_PAGE_SIZE,
					     EFI_MEMORY_RP | EFI_MEMORY_XP);
	if (ret != EFI_SUCCESS) {
		efi_st_error("Cannot clear RP and XP\n");
		return EFI_ST_FAILURE;
	}
	if (check_attrs(code, EFI_MEMORY_RO) || check_attrs(data, EFI_MEMORY_XP))
		return EFI_ST_FAILURE;

	/* Read protection cannot be set */
	ret = mattr->set_memory_attributes(mattr, data, EFI_PAGE_SIZE,
					   EFI_MEMORY_RP);
	if (ret != EFI_UNSUPPORTED) {
		efi_st_error("Setting RP should be unsupported\n");
		return EFI_ST_FAILURE;
	}

	/* Bad ranges and attributes are rejected */
	ret = mattr->set_memory_attributes(mattr, data + 1, EFI_PAGE_SIZE,
					   EFI_MEMORY_XP);
	if (ret != EFI_INVALID_PARAMETER) {
		efi_st_error("An unaligned range should be rejected\n");
		return EFI_ST_FAILURE;
	}
	ret = mattr->clear_memory_attributes(mattr, data, EFI_PAGE_SIZE,
					     EFI_MEMORY_WB);
	if (ret != EFI_INVALID_PARAMETER) {
		efi_st_error("A cacheability attribute should be rejected\n");
		return EFI_ST_FAILURE;
	}

	return EFI_ST_SUCCESS;
}
EFI_UNIT_TEST(memattr) = {
	.name = "memory attribute protocol",
	.phase = EFI_EXECUTE_BEFORE_BOOTTIME_EXIT,
	.setup = setup,
	.execute = execute,
	.teardown = teardown,
};
