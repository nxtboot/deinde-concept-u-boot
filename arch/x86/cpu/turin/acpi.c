// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * ACPI tables for AMD EPYC Turin
 *
 * There is no ACPI hardware setup here (no PM1 blocks, GPEs or SCI), so the
 * FADT declares a hardware-reduced platform. The OS finds the PCIe root
 * complexes in the DSDT, the ECAM in the MCFG, the FCH's I/O APIC in the
 * MADT and the FCH's HPET for its timer.
 */

#define LOG_CATEGORY LOGC_ACPI

#include <acpi/acpi_table.h>
#include <asm/acpi_table.h>
#include <asm/io.h>
#include <asm/ioapic.h>
#include <asm/lapic.h>
#include <asm/mpspec.h>
#include <asm/tables.h>
#include <asm/arch/cpu.h>
#include <asm/arch/global_nvs.h>
#include <dm/acpi.h>
#include <linux/sizes.h>

#define IO_PORT_RESET		0xcf9
#define RST_CPU			BIT(2)
#define SYS_RST			BIT(1)

#define FCH_IOAPIC_PINS		24
#define NBIO_IOAPIC_PINS	32
#define PCI_BUS_COUNT		0x100
#define TURIN_BUSES_PER_ROOT	0x20

void acpi_fill_fadt(struct acpi_fadt *fadt)
{
	fadt->iapc_boot_arch = ACPI_FADT_LEGACY_DEVICES;
	fadt->flags = ACPI_FADT_WBINVD | ACPI_FADT_C1_SUPPORTED |
		ACPI_FADT_RESET_REGISTER | ACPI_FADT_HW_REDUCED_ACPI;

	fadt->reset_reg.space_id = ACPI_ADDRESS_SPACE_IO;
	fadt->reset_reg.bit_width = 8;
	fadt->reset_reg.access_size = ACPI_ACCESS_SIZE_BYTE_ACCESS;
	fadt->reset_reg.addrl = IO_PORT_RESET;
	fadt->reset_value = SYS_RST | RST_CPU;
}

int acpi_create_gnvs(struct acpi_global_nvs *gnvs)
{
	return 0;
}

void *acpi_fill_madt(struct acpi_madt *madt, struct acpi_ctx *ctx)
{
	void *current = ctx->current;
	uint gsi;
	int bus;

	madt->lapic_addr = LAPIC_DEFAULT_BASE;
	madt->flags = ACPI_MADT_PCAT_COMPAT;

	current += acpi_create_madt_lapics(current);
	current += acpi_create_madt_ioapic(current,
					   io_apic_read(IO_APIC_ID) >> 24,
					   IO_APIC_ADDR, 0);

	/* then each root complex's, in bus order, after the FCH's 24 pins */
	gsi = FCH_IOAPIC_PINS;
	for (bus = 0; bus < PCI_BUS_COUNT; bus += TURIN_BUSES_PER_ROOT) {
		u32 addr;
		uint id;

		if (turin_get_ioapic(bus, &addr, &id))
			continue;
		current += acpi_create_madt_ioapic(current, id, addr, gsi);
		gsi += NBIO_IOAPIC_PINS;
	}
	current += acpi_create_madt_irqoverride(current, 0, 0, 2, 0);
	current += acpi_create_madt_lapic_nmi(current, 0xff, 0, 1);

	return current;
}

int acpi_fill_mcfg(struct acpi_ctx *ctx)
{
	size_t size;

	size = acpi_create_mcfg_mmconfig(ctx->current, CONFIG_PCIE_ECAM_BASE,
					 0, 0, (CONFIG_PCIE_ECAM_SIZE >> 20) - 1);
	acpi_inc(ctx, size);

	return 0;
}

static int turin_write_hpet(struct acpi_ctx *ctx,
			    const struct acpi_writer *entry)
{
	return acpi_write_hpet(ctx);
}
ACPI_WRITER(5hpet, "HPET", turin_write_hpet, 0);
