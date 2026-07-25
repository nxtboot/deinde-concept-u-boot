// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Modified from the apollolake version, with register details from
 * coreboot's Alder Lake support
 */

#define LOG_CATEGORY LOGC_ACPI

#include <cpu.h>
#include <dm.h>
#include <intel_gnvs.h>
#include <log.h>
#include <acpi/acpi_s3.h>
#include <acpi/acpi_table.h>
#include <asm/acpi_table.h>
#include <asm/intel_acpi.h>
#include <asm/io.h>
#include <asm/mpspec.h>
#include <asm/arch/iomap.h>
#include <dm/acpi.h>
#include <dm/uclass-internal.h>
#include <power/acpi_pmc.h>

/*
 * The SCI IRQ-select field lives in the PMC's ACTL register, in its
 * power-management MMIO space; U-Boot uses the default of IRQ 9
 */
#define PWRM_ACTL		0x1bd8
#define ACTL_SCI_IRQ_SEL_MASK	7

int arch_read_sci_irq_select(void)
{
	return readl(PCH_PWRM_BASE_ADDRESS + PWRM_ACTL) &
		ACTL_SCI_IRQ_SEL_MASK;
}

int arch_write_sci_irq_select(uint scis)
{
	clrsetbits_le32(PCH_PWRM_BASE_ADDRESS + PWRM_ACTL,
			ACTL_SCI_IRQ_SEL_MASK, scis);

	return 0;
}

#ifdef CONFIG_CHROMEOS
/**
 * chromeos_init_acpi() - Initialise basic data to boot Chrome OS
 *
 * This tells Chrome OS to boot in developer mode
 *
 * @cros: Structure to initialise
 */
static void chromeos_init_acpi(struct chromeos_acpi_gnvs *cros)
{
	cros->active_main_fw = 1; /* A */
	cros->switches = CHSW_DEVELOPER_SWITCH;
	cros->main_fw_type = 2; /* Developer */
}
#endif

int acpi_create_gnvs(struct acpi_global_nvs *gnvs)
{
	struct udevice *cpu;
	int ret;

	memset(gnvs, '\0', sizeof(*gnvs));

	/*
	 * The chromeos member only exists in the struct when CONFIG_CHROMEOS
	 * is enabled, so IS_ENABLED() cannot be used here
	 */
#ifdef CONFIG_CHROMEOS
	chromeos_init_acpi(&gnvs->chromeos);
#endif

	/* Set unknown wake source */
	gnvs->pm1i = ~0ULL;

	/* CPU core count */
	gnvs->pcnt = 1;
	ret = uclass_find_first_device(UCLASS_CPU, &cpu);
	if (cpu) {
		ret = cpu_get_count(cpu);
		if (ret > 0)
			gnvs->pcnt = ret;
	}

	return 0;
}

int arch_madt_sci_irq_polarity(int sci)
{
	return MP_IRQ_POLARITY_HIGH;
}

/* This SoC's GPE0 status registers are at 0x60, not the generic 0x20 */
#define ADL_GPE0_STS	0x60

void acpi_fill_fadt(struct acpi_fadt *fadt)
{
	intel_acpi_fill_fadt(fadt);

	fadt->gpe0_blk = IOMAP_ACPI_BASE + ADL_GPE0_STS;

	fadt->pm_tmr_blk = IOMAP_ACPI_BASE + PM1_TMR;

	fadt->p_lvl2_lat = ACPI_FADT_C2_NOT_SUPPORTED;
	fadt->p_lvl3_lat = ACPI_FADT_C3_NOT_SUPPORTED;

	fadt->pm_tmr_len = 4;
	fadt->duty_width = 3;

	fadt->iapc_boot_arch = ACPI_FADT_LEGACY_DEVICES | ACPI_FADT_8042;

	fadt->x_pm_tmr_blk.space_id = 1;
	fadt->x_pm_tmr_blk.bit_width = fadt->pm_tmr_len * 8;
	fadt->x_pm_tmr_blk.addrl = IOMAP_ACPI_BASE + PM1_TMR;

	fadt->preferred_pm_profile = ACPI_PM_MOBILE;
}
