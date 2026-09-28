// SPDX-License-Identifier: GPL-2.0
/*
 * Generation of tables for particular device types
 *
 * Copyright 2025 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY	LOGC_ACPI

#include <bootstage.h>
#include <dm.h>
#include <efi_loader.h>
#include <mapmem.h>
#include <tables_csum.h>
#include <video.h>
#include <acpi/acpi_table.h>

int acpi_write_bgrt(struct acpi_ctx *ctx)
{
	struct udevice *dev;
	struct acpi_bgrt *bgrt;
	efi_status_t eret;
	void *logo, *buf;
	bool have_video;
	int size;

	/* If video is available, use the screen size to centre the logo */
	have_video = !uclass_first_device_err(UCLASS_VIDEO, &dev);

	if (!IS_ENABLED(CONFIG_VIDEO))
		return -ENOENT;

	logo = video_image_get(bgrt, &size);

	/* If there's no logo data, there's nothing to report */
	if (!logo)
		return -ENOENT;

	bgrt = ctx->current;
	ctx->tab_start = ctx->current;
	memset(bgrt, '\0', sizeof(struct acpi_table_iort));

	acpi_fill_header(&bgrt->header, "BGRT");
	bgrt->version = 1;

	/* Status: Bit 0 (Displayed) = 1, Bits 1-2 (Orientation) = 0 */
	bgrt->status = 1;

	/* Image Type: 0 = Bitmap */
	bgrt->image_type = 0;

	/* Mark space used for tables */
	eret = efi_allocate_pool(EFI_BOOT_SERVICES_DATA, size, &buf);
	if (eret)
		return -ENOMEM;
	memcpy(buf, logo, size);

	/* The physical address of the in-memory logo bitmap */
	bgrt->addr = nomap_to_sysmem(buf);

	/* Calculate offsets to center the logo on the screen */
	bgrt->offset_x = 0;
	bgrt->offset_y = 0;

	/*
	 * centering is disabled for now, since it seems to be done by the
	 * startup code
	 */
	if (0 && IS_ENABLED(CONFIG_VIDEO) && have_video) {
		struct video_priv *priv = dev_get_uclass_priv(dev);
		ulong width, height;
		uint bpix;

		video_bmp_get_info(logo, &width, &height, &bpix);

		if (priv->xsize > width)
			bgrt->offset_x = (priv->xsize - width) / 2;
		if (priv->ysize > height)
			bgrt->offset_y = (priv->ysize - height) / 2;
	}
	acpi_inc(ctx, sizeof(*bgrt));

	/* Calculate length and checksum */
	bgrt->header.length = (ulong)ctx->current - (ulong)bgrt;
	acpi_update_checksum(&bgrt->header);
	log_debug("BGRT at %p length %x logo copied to bs-data at %p\n", bgrt,
		  bgrt->header.length, buf);
	acpi_add_table(ctx, bgrt);

	return 0;
}

int acpi_write_fpdt(struct acpi_ctx *ctx, u64 uboot_start)
{
	struct acpi_table_header *header;
	struct acpi_fpdt_boot *rec;
	struct acpi_fpdt_ptr *ptr;
	struct acpi_fpdt *fpdt;
	struct acpi_fbpt *fbpt;
	u64 current_time;
	int size;

	/* The FPDT holds just a pointer record, to the FBPT */
	fpdt = ctx->current;
	header = &fpdt->header;
	size = sizeof(struct acpi_fpdt) + sizeof(struct acpi_fpdt_ptr);
	memset(fpdt, '\0', size);
	acpi_fill_header(header, "FPDT");
	header->length = size;
	header->revision = 1;  /* ACPI 6.4+: 1 */
	ptr = (struct acpi_fpdt_ptr *)(fpdt + 1);
	ptr->hdr.type = FPDT_REC_PTR_BOOT;
	ptr->hdr.length = sizeof(struct acpi_fpdt_ptr);
	ptr->hdr.revision = 1;
	acpi_inc_align(ctx, size);

	/* The FBPT follows, holding the boot performance record */
	fbpt = ctx->current;
	size = sizeof(struct acpi_fbpt) + sizeof(struct acpi_fpdt_boot);
	memset(fbpt, '\0', size);
	memcpy(fbpt->signature, "FBPT", ACPI_NAME_LEN);
	fbpt->length = size;
	rec = (struct acpi_fpdt_boot *)(fbpt + 1);
	rec->hdr.type = FPDT_REC_BOOT;
	rec->hdr.length = sizeof(struct acpi_fpdt_boot);
	rec->hdr.revision = 2;  /* FPDT Boot Performance Record revision */

	/* Fill in timing data, in nanoseconds */
	current_time = timer_get_boot_us() * 1000ULL;
	rec->reset_end = uboot_start * 1000ULL;
	rec->loader_start = current_time;
	rec->loader_exec = current_time;
	rec->ebs_entry = current_time;
	rec->ebs_exit = current_time;
	acpi_inc_align(ctx, size);

	ptr->addr = nomap_to_sysmem(fbpt);
	header->checksum = table_compute_checksum(fpdt, header->length);
	acpi_add_table(ctx, fpdt);

	return 0;
}

struct acpi_fpdt_boot *acpi_get_fpdt_boot(void)
{
	struct acpi_table_header *header;
	struct acpi_fpdt_ptr *ptr;
	struct acpi_fpdt *fpdt;
	struct acpi_fbpt *fbpt;

	header = acpi_find_table("FPDT");
	if (!header)
		return NULL;

	fpdt = (struct acpi_fpdt *)header;
	ptr = (struct acpi_fpdt_ptr *)(fpdt + 1);
	if (header->length < sizeof(*fpdt) + sizeof(*ptr) ||
	    ptr->hdr.type != FPDT_REC_PTR_BOOT)
		return NULL;
	fbpt = nomap_sysmem(ptr->addr, 0);

	return (struct acpi_fpdt_boot *)(fbpt + 1);
}

int acpi_fix_fpdt_checksum(void)
{
	struct acpi_table_header *header;

	header = acpi_find_table("FPDT");
	if (!header)
		return -ENOENT;

	header->checksum = 0;
	header->checksum = table_compute_checksum(header, header->length);

	return 0;
}

void acpi_final_fpdt(void)
{
	struct acpi_fpdt_boot *fpdt;

	if (IS_ENABLED(CONFIG_TARGET_QEMU_VIRT))
		return;

	fpdt = acpi_get_fpdt_boot();
	if (fpdt) {
		u64 time;

		/* The record is in the FBPT, which has no checksum */
		time = timer_get_boot_us() * 1000ULL;
		fpdt->ebs_entry = time;
		fpdt->ebs_exit = time;
	}
}
