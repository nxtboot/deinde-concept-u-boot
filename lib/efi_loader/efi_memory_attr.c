// SPDX-License-Identifier: GPL-2.0+
/*
 * EFI_MEMORY_ATTRIBUTE_PROTOCOL
 *
 * Lets a payload mark memory read-only or non-executable, and read those
 * attributes back, by changing the page tables. Only the RO and XP attributes
 * are supported; RP (no access) is not.
 *
 * Copyright 2026 Google LLC
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_loader.h>
#include <asm/system.h>
#include <asm/armv8/mmu.h>

static const efi_guid_t efi_guid_memory_attribute =
	EFI_MEMORY_ATTRIBUTE_PROTOCOL_GUID;

#define SUPPORTED_ATTRS	(EFI_MEMORY_RO | EFI_MEMORY_XP)

/* The PTE bits which make a page non-executable at the current EL */
static u64 xp_pte_bits(void)
{
	if (get_effective_el() == 1)
		return PTE_BLOCK_PXN | PTE_BLOCK_UXN;

	return PTE_BLOCK_UXN;
}

/**
 * pte_to_efi() - Convert PTE attribute bits to EFI memory attributes
 *
 * @pte: PTE attribute bits, as from mmu_get_page_attrs()
 * Return: EFI_MEMORY_RO and/or EFI_MEMORY_XP
 */
static u64 pte_to_efi(u64 pte)
{
	u64 attrs = 0;

	if (pte & PTE_BLOCK_RO)
		attrs |= EFI_MEMORY_RO;
	if ((pte & xp_pte_bits()) == xp_pte_bits())
		attrs |= EFI_MEMORY_XP;

	return attrs;
}

/**
 * check_range() - Check that a range is valid for these calls
 *
 * @base: Start of the range
 * @length: Length of the range in bytes
 * @attrs: Attributes requested (may be zero for a get)
 * Return: status code
 */
static efi_status_t check_range(efi_physical_addr_t base, u64 length,
				u64 attrs)
{
	if (!length || (base & (EFI_PAGE_SIZE - 1)) ||
	    (length & (EFI_PAGE_SIZE - 1)))
		return EFI_INVALID_PARAMETER;
	if (attrs & ~SUPPORTED_ATTRS)
		return attrs & ~(SUPPORTED_ATTRS | EFI_MEMORY_RP) ?
			EFI_INVALID_PARAMETER : EFI_UNSUPPORTED;

	return EFI_SUCCESS;
}

/**
 * get_memory_attributes() - Read the attributes common to a range
 *
 * @this: Protocol instance
 * @base: Start of the range
 * @length: Length of the range in bytes
 * @attributes: Returns the attributes set on every page of the range
 * Return: status code
 */
static efi_status_t EFIAPI
get_memory_attributes(struct efi_memory_attribute_protocol *this,
		      efi_physical_addr_t base, u64 length, u64 *attributes)
{
	efi_status_t ret;
	u64 attrs = SUPPORTED_ATTRS;
	u64 addr;

	EFI_ENTRY("%p, %llx, %llx, %p", this, base, length, attributes);
	if (!attributes) {
		ret = EFI_INVALID_PARAMETER;
		goto out;
	}
	ret = check_range(base, length, 0);
	if (ret != EFI_SUCCESS)
		goto out;
	for (addr = base; addr < base + length; addr += EFI_PAGE_SIZE) {
		u64 pte;

		if (mmu_get_page_attrs(addr, &pte)) {
			ret = EFI_NO_MAPPING;
			goto out;
		}
		attrs &= pte_to_efi(pte);
	}
	*attributes = attrs;
out:
	return EFI_EXIT(ret);
}

/**
 * change_attributes() - Set or clear attributes on a range
 *
 * Each page keeps its memory type and gains or loses the RO and XP bits.
 *
 * @base: Start of the range
 * @length: Length of the range in bytes
 * @attrs: Attributes to set or clear
 * @set: true to set them, false to clear them
 * Return: status code
 */
static efi_status_t change_attributes(efi_physical_addr_t base, u64 length,
				      u64 attrs, bool set)
{
	efi_status_t ret;
	u64 addr;

	ret = check_range(base, length, attrs);
	if (ret != EFI_SUCCESS)
		return ret;
	if (!attrs)
		return EFI_SUCCESS;
	for (addr = base; addr < base + length; addr += EFI_PAGE_SIZE) {
		u64 pte, bits = 0;

		if (mmu_get_page_attrs(addr, &pte))
			return EFI_NO_MAPPING;
		if (attrs & EFI_MEMORY_RO)
			bits |= PTE_BLOCK_RO;
		if (attrs & EFI_MEMORY_XP)
			bits |= xp_pte_bits();
		pte = set ? pte | bits : pte & ~bits;
		mmu_change_region_attr_nobreak(addr, EFI_PAGE_SIZE, pte);
	}

	return EFI_SUCCESS;
}

/**
 * set_memory_attributes() - Set attributes on a range
 *
 * @this: Protocol instance
 * @base: Start of the range
 * @length: Length of the range in bytes
 * @attributes: Attributes to set
 * Return: status code
 */
static efi_status_t EFIAPI
set_memory_attributes(struct efi_memory_attribute_protocol *this,
		      efi_physical_addr_t base, u64 length, u64 attributes)
{
	EFI_ENTRY("%p, %llx, %llx, %llx", this, base, length, attributes);

	return EFI_EXIT(change_attributes(base, length, attributes, true));
}

/**
 * clear_memory_attributes() - Clear attributes on a range
 *
 * @this: Protocol instance
 * @base: Start of the range
 * @length: Length of the range in bytes
 * @attributes: Attributes to clear
 * Return: status code
 */
static efi_status_t EFIAPI
clear_memory_attributes(struct efi_memory_attribute_protocol *this,
			efi_physical_addr_t base, u64 length, u64 attributes)
{
	EFI_ENTRY("%p, %llx, %llx, %llx", this, base, length, attributes);

	return EFI_EXIT(change_attributes(base, length, attributes, false));
}

static const struct efi_memory_attribute_protocol efi_memory_attr_protocol = {
	.get_memory_attributes = get_memory_attributes,
	.set_memory_attributes = set_memory_attributes,
	.clear_memory_attributes = clear_memory_attributes,
};

efi_status_t efi_memory_attr_register(void)
{
	efi_status_t ret;

	ret = efi_add_protocol(efis->bs.root, &efi_guid_memory_attribute,
			       (void *)&efi_memory_attr_protocol);
	if (ret != EFI_SUCCESS)
		log_err("Cannot install EFI_MEMORY_ATTRIBUTE_PROTOCOL\n");

	return ret;
}
