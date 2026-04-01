// SPDX-License-Identifier: GPL-2.0
/*
 * Utility functions for ACPI
 *
 * Copyright 2023 Google LLC
 */

#define LOG_CATEGORY	LOGC_ACPI

#include <errno.h>
#include <mapmem.h>
#include <tables_csum.h>
#include <version_string.h>
#include <acpi/acpi_table.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

/*
 * OEM_REVISION is 32-bit unsigned number. It should be increased only when
 * changing software version. Therefore it should not depend on build time.
 * U-Boot calculates it from U-Boot version and represent it in hexadecimal
 * notation. As U-Boot version is in form year.month set low 8 bits to 0x01
 * to have valid date. So for U-Boot version 2021.04 OEM_REVISION is set to
 * value 0x20210401.
 */
#define OEM_REVISION ((((version_num / 1000) % 10) << 28) | \
		      (((version_num / 100) % 10) << 24) | \
		      (((version_num / 10) % 10) << 20) | \
		      ((version_num % 10) << 16) | \
		      (((version_num_patch / 10) % 10) << 12) | \
		      ((version_num_patch % 10) << 8) | \
		      0x01)

void acpi_update_checksum(struct acpi_table_header *header)
{
	header->checksum = 0;
	header->checksum = table_compute_checksum(header, header->length);
}

static bool acpi_valid_rsdp(struct acpi_rsdp *rsdp)
{
	if (strncmp((char *)rsdp, RSDP_SIG, sizeof(RSDP_SIG) - 1) != 0)
		return false;

	debug("Looking on %p for valid checksum\n", rsdp);

	if (table_compute_checksum((void *)rsdp, 20) != 0)
		return false;
	debug("acpi rsdp checksum 1 passed\n");

	if (rsdp->revision > 1 &&
	    table_compute_checksum((void *)rsdp, rsdp->length))
		return false;
	debug("acpi rsdp checksum 2 passed\n");

	return true;
}

/**
 * setup_search() - Set up for searching through the RSDT/XSDT
 *
 * Looks for XSDT first and uses those entries if available, else RSDT
 *
 * @rsdtp: Returns RSDT if present
 * @xsdtp: Returns XSDT if present
 * Return: number of entries in table, -ENOENT if there is no RSDP, -EINVAL if
 *	the RSDP is invalid, -ENOTSYNC if both tables exist and their counts
 *	disagree
 */
static int setup_search(struct acpi_rsdt **rsdtp, struct acpi_xsdt **xsdtp)
{
	struct acpi_rsdp *rsdp;
	struct acpi_rsdt *rsdt = NULL;
	struct acpi_xsdt *xsdt = NULL;
	int len, count = 0;


	rsdp = map_sysmem(gd_acpi_start(), 0);
	if (!rsdp)
		return -ENOENT;
	if (!acpi_valid_rsdp(rsdp))
		return -EINVAL;
	if (rsdp->xsdt_address) {
		xsdt = nomap_sysmem(rsdp->xsdt_address, 0);
		len = xsdt->header.length - sizeof(xsdt->header);
		count = len / sizeof(u64);
	}

	if (rsdp->rsdt_address) {
		int rcount;

		rsdt = nomap_sysmem(rsdp->rsdt_address, 0);
		len = rsdt->header.length - sizeof(rsdt->header);
		rcount = len / sizeof(u32);
		if (xsdt) {
			if (rcount != count)
				return -ENOTSYNC;
		} else {
			count = rcount;
		}
	}
	*rsdtp = rsdt;
	*xsdtp = xsdt;

	return count;
}

struct acpi_table_header *acpi_find_table(const char *sig)
{
	struct acpi_rsdt *rsdt;
	struct acpi_xsdt *xsdt;
	int i, count, ret;

	ret = setup_search(&rsdt, &xsdt);
	if (ret < 0) {
		log_warning("acpi: Failed to find tables (err=%d)\n", ret);
		return NULL;
	}
	if (!ret)
		return NULL;
	count = ret;

	if (!strcmp("RSDT", sig))
		return &rsdt->header;
	if (!strcmp("XSDT", sig))
		return &xsdt->header;

	for (i = 0; i < count; i++) {
		struct acpi_table_header *hdr;

		if (xsdt)
			hdr = nomap_sysmem(xsdt->entry[i], 0);
		else
			hdr = nomap_sysmem(rsdt->entry[i], 0);
		if (!memcmp(hdr->signature, sig, ACPI_NAME_LEN))
			return hdr;
		if (!memcmp(hdr->signature, "FACP", ACPI_NAME_LEN)) {
			struct acpi_fadt *fadt = (struct acpi_fadt *)hdr;

			if (!memcmp(sig, "DSDT", ACPI_NAME_LEN)) {
				void *dsdt;

				if (fadt->header.revision >= 3 && fadt->x_dsdt)
					dsdt = nomap_sysmem(fadt->x_dsdt, 0);
				else if (fadt->dsdt)
					dsdt = nomap_sysmem(fadt->dsdt, 0);
				else
					dsdt = NULL;
				return dsdt;
			}

			if (!memcmp(sig, "FACS", ACPI_NAME_LEN)) {
				void *facs;

				if (fadt->header.revision >= 3 &&
				    fadt->x_firmware_ctrl)
					facs = nomap_sysmem(fadt->x_firmware_ctrl, 0);
				else if (fadt->firmware_ctrl)
					facs = nomap_sysmem(fadt->firmware_ctrl, 0);
				else
					facs = NULL;
				return facs;
			}
		}
	}

	return NULL;
}

void acpi_fill_header(struct acpi_table_header *header, char *signature)
{
	memcpy(header->signature, signature, 4);
	memcpy(header->oem_id, OEM_ID, 6);
	memcpy(header->oem_table_id, OEM_TABLE_ID, 8);
	header->oem_revision = OEM_REVISION;
	memcpy(header->creator_id, ACPI_CREATOR_ID, 4);
	header->creator_revision = ACPI_CREATOR_REVISION;
}

void acpi_align(struct acpi_ctx *ctx)
{
	ctx->current = (void *)ALIGN((ulong)ctx->current, 16);
}

void acpi_align64(struct acpi_ctx *ctx)
{
	ctx->current = (void *)ALIGN((ulong)ctx->current, 64);
}

void acpi_inc(struct acpi_ctx *ctx, uint amount)
{
	ctx->current += amount;
}

void acpi_inc_align(struct acpi_ctx *ctx, uint amount)
{
	ctx->current += amount;
	acpi_align(ctx);
}

/**
 * Add an ACPI table to the RSDT (and XSDT) structure, recalculate length
 * and checksum.
 */
int acpi_add_table(struct acpi_ctx *ctx, void *table)
{
	int i, entries_num;
	struct acpi_rsdt *rsdt;
	struct acpi_xsdt *xsdt;

	if (!ctx->rsdt && !ctx->xsdt) {
		log_err("ACPI: Error: no RSDT / XSDT\n");
		return -EINVAL;
	}

	/* On legacy x86 platforms the RSDT is mandatory while the XSDT is not.
	 * On other platforms there might be no memory below 4GiB, thus RSDT is NULL.
	 */
	if (ctx->rsdt) {
		rsdt = ctx->rsdt;

		/* This should always be MAX_ACPI_TABLES */
		entries_num = ARRAY_SIZE(rsdt->entry);

		for (i = 0; i < entries_num; i++) {
			if (rsdt->entry[i] == 0)
				break;
		}

		if (i >= entries_num) {
			log_err("ACPI: Error: too many tables\n");
			return -E2BIG;
		}

		/* Add table to the RSDT */
		rsdt->entry[i] = nomap_to_sysmem(table);

		/* Fix RSDT length or the kernel will assume invalid entries */
		rsdt->header.length = sizeof(struct acpi_table_header) +
					(sizeof(u32) * (i + 1));

		/* Re-calculate checksum */
		acpi_update_checksum(&rsdt->header);
	}

	if (ctx->xsdt) {
		/*
		 * And now the same thing for the XSDT. We use the same index as for
		 * now we want the XSDT and RSDT to always be in sync in U-Boot
		 */
		xsdt = ctx->xsdt;

		/* This should always be MAX_ACPI_TABLES */
		entries_num = ARRAY_SIZE(xsdt->entry);

		for (i = 0; i < entries_num; i++) {
			if (xsdt->entry[i] == 0)
				break;
		}

		if (i >= entries_num) {
			log_err("ACPI: Error: too many tables\n");
			return -E2BIG;
		}

		/* Add table to the XSDT */
		xsdt->entry[i] = nomap_to_sysmem(table);

		/* Fix XSDT length */
		xsdt->header.length = sizeof(struct acpi_table_header) +
					(sizeof(u64) * (i + 1));

		/* Re-calculate checksum */
		acpi_update_checksum(&xsdt->header);
	}

	return 0;
}

void *acpi_get_end(void)
{
	const struct acpi_table_header *end;
	struct acpi_rsdt *rsdt;
	struct acpi_xsdt *xsdt;
	int i, count;

	count = setup_search(&rsdt, &xsdt);
	if (!count)
		return NULL;

	end = xsdt ? &xsdt->header : &rsdt->header;
	for (i = 0; i < count; i++) {
		const struct acpi_table_header *hdr;

		if (xsdt)
			hdr = nomap_sysmem(xsdt->entry[i], 0);
		else
			hdr = nomap_sysmem(rsdt->entry[i], 0);
		end = max(hdr, end);
	}

	return (void *)end + end->length;
}
