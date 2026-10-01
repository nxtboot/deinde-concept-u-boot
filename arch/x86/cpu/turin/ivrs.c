// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * IVRS table for the IOMMUs of an AMD EPYC Turin processor
 *
 * Each big root complex has an IOMMU, which also covers the small root
 * complex paired with it. The table describes each IOMMU's registers and
 * features and lists the devices whose transactions it translates: every
 * PCI device on the buses it covers, their I/O APICs and, for the root
 * complex with the FCH, the FCH's I/O APIC and HPET
 */

#define LOG_CATEGORY LOGC_ACPI

#include <acpi/acpi_table.h>
#include <log.h>
#include <tables_csum.h>
#include <asm/io.h>
#include <asm/ioapic.h>
#include <asm/pci.h>
#include <asm/arch/cpu.h>
#include <dm/acpi.h>
#include <linux/bitfield.h>

/* IOMMU capability registers, in its config space */
#define IOMMU_CAP_HDR		0x40
#define IOMMU_CAP_IOTLB_SUP	BIT(24)
#define IOMMU_CAP_EFR_SUP	BIT(27)
#define IOMMU_CAP_RANGE		0x4c
#define IOMMU_RANGE_UNIT_ID	GENMASK(4, 0)
#define IOMMU_CAP_MISC		0x50
#define IOMMU_MISC_MSI_NUM	GENMASK(4, 0)
#define IOMMU_MISC_MSI_NUM_PPR	GENMASK(31, 27)

/* IOMMU MMIO registers */
#define IOMMU_MMIO_CONTROL	0x18
#define IOMMU_CTRL_HT_TUN_EN	BIT(1)
#define IOMMU_CTRL_PASS_PW	BIT(8)
#define IOMMU_CTRL_RES_PASS_PW	BIT(9)
#define IOMMU_CTRL_COHERENT	BIT(10)
#define IOMMU_CTRL_ISOC		BIT(11)
#define IOMMU_MMIO_EFR		0x30
#define IOMMU_EFR_PREF_SUP	BIT(0)
#define IOMMU_EFR_PPR_SUP	BIT(1)
#define IOMMU_EFR_XT_SUP	BIT(2)
#define IOMMU_EFR_NX_SUP	BIT(3)
#define IOMMU_EFR_GT_SUP	BIT(4)
#define IOMMU_EFR_IA_SUP	BIT(6)
#define IOMMU_EFR_GA_SUP	BIT(7)
#define IOMMU_EFR_HE_SUP	BIT(8)
#define IOMMU_EFR_HATS		GENMASK(11, 10)
#define IOMMU_EFR_GATS		GENMASK(13, 12)
#define IOMMU_EFR_GLX		GENMASK(15, 14)
#define IOMMU_EFR_PAS_MAX	GENMASK_ULL(36, 32)
#define IOMMU_MMIO_EFR2		0x1a0
#define IOMMU_MMIO_COUNTER_CFG	0x4000
#define IOMMU_CNT_COUNTERS	GENMASK(10, 7)
#define IOMMU_CNT_BANKS		GENMASK(17, 12)

/* Fields of the type 10h block's feature word */
#define IVHD_FEAT_HATS		GENMASK(31, 30)
#define IVHD_FEAT_GATS		GENMASK(29, 28)
#define IVHD_FEAT_MSI_NUM_PPR	GENMASK(27, 23)
#define IVHD_FEAT_PN_BANKS	GENMASK(22, 17)
#define IVHD_FEAT_PN_COUNTERS	GENMASK(16, 13)
#define IVHD_FEAT_PAS_MAX	GENMASK(12, 8)
#define IVHD_FEAT_HE_SUP	BIT(7)
#define IVHD_FEAT_GA_SUP	BIT(6)
#define IVHD_FEAT_IA_SUP	BIT(5)
#define IVHD_FEAT_GLX		GENMASK(4, 3)
#define IVHD_FEAT_GT_SUP	BIT(2)
#define IVHD_FEAT_NX_SUP	BIT(1)
#define IVHD_FEAT_XT_SUP	BIT(0)

/* Fields of the type 11h block's attribute word */
#define IVHD_ATTR_MSI_NUM_PPR	GENMASK(31, 27)
#define IVHD_ATTR_PN_BANKS	GENMASK(22, 17)
#define IVHD_ATTR_PN_COUNTERS	GENMASK(16, 13)

#define IVHD_INFO_UNIT_ID_SHIFT	8

/* Devices as the IOMMU sees them: bus, device and function */
#define IVHD_DEVID(bus, dev, fn)	((bus) << 8 | (dev) << 3 | (fn))
#define FCH_SMBUS_DEVID		IVHD_DEVID(0, 0x14, 0)

/* What the FCH's I/O APIC passes through untranslated */
#define FCH_IOAPIC_DTE		(IVHD_DTE_LINT1_PASS | IVHD_DTE_LINT0_PASS | \
				 IVHD_DTE_SYS_MGT_NO_TRANS | IVHD_DTE_NMI_PASS | \
				 IVHD_DTE_EXT_INT_PASS | IVHD_DTE_INIT_PASS)

/* What is read from an IOMMU to describe it */
struct iommu_info {
	int busno;
	u32 base;
	u32 cap;
	u32 range;
	u32 misc;
	u64 control;
	u64 efr;
	u64 efr2;
	u64 counter_cfg;
};

static void iommu_read(struct iommu_info *info, int busno, u32 base)
{
	pci_dev_t bdf = PCI_BDF(busno, 0, 2);
	ulong val;

	info->busno = busno;
	info->base = base;
	pci_x86_read_config(bdf, IOMMU_CAP_HDR, &val, PCI_SIZE_32);
	info->cap = val;
	pci_x86_read_config(bdf, IOMMU_CAP_RANGE, &val, PCI_SIZE_32);
	info->range = val;
	pci_x86_read_config(bdf, IOMMU_CAP_MISC, &val, PCI_SIZE_32);
	info->misc = val;
	info->control = readq(base + IOMMU_MMIO_CONTROL);
	info->efr = readq(base + IOMMU_MMIO_EFR);
	info->efr2 = readq(base + IOMMU_MMIO_EFR2);
	info->counter_cfg = readq(base + IOMMU_MMIO_COUNTER_CFG);
}

static void *add_range(void *p, int first_bus, int last_bus)
{
	struct acpi_ivhd_dev *dev = p;

	memset(dev, '\0', 2 * sizeof(*dev));
	dev->type = IVHD_DEV_START_RANGE;
	dev->dev_id = IVHD_DEVID(first_bus, 0, 3);
	dev++;
	dev->type = IVHD_DEV_END_RANGE;
	dev->dev_id = IVHD_DEVID(last_bus, 0x1f, 6);

	return dev + 1;
}

static void *add_special(void *p, uint variety, uint handle, uint source_id,
			 uint setting)
{
	struct acpi_ivhd_special *dev = p;

	memset(dev, '\0', sizeof(*dev));
	dev->type = IVHD_DEV_SPECIAL;
	dev->setting = setting;
	dev->handle = handle;
	dev->source_id = source_id;
	dev->variety = variety;

	return dev + 1;
}

/* Add the I/O APIC of a root complex, which appears as its device 0.1 */
static void *add_ioapic(void *p, int busno)
{
	u32 addr;
	uint id;

	if (turin_get_ioapic(busno, &addr, &id))
		return p;

	return add_special(p, IVHD_SPECIAL_IOAPIC, id, IVHD_DEVID(busno, 0, 1),
			   0);
}

/* Add the devices an IOMMU covers: its own buses and its partner's */
static void *add_devices(void *p, int busno)
{
	int partner = turin_get_paired_bus(busno);

	p = add_range(p, busno, busno + TURIN_BUSES_PER_ROOT - 1);
	if (partner >= 0)
		p = add_range(p, partner, partner + TURIN_BUSES_PER_ROOT - 1);
	p = add_ioapic(p, busno);
	if (partner >= 0)
		p = add_ioapic(p, partner);
	if (busno == FCH_ROOT_BUS) {
		p = add_special(p, IVHD_SPECIAL_HPET, 0, FCH_SMBUS_DEVID, 0);
		p = add_special(p, IVHD_SPECIAL_IOAPIC,
				io_apic_read(IO_APIC_ID) >> 24, FCH_SMBUS_DEVID,
				FCH_IOAPIC_DTE);
	}

	return p;
}

static u8 ivhd_flags(const struct iommu_info *info, bool legacy)
{
	u8 flags = 0;

	if (legacy) {
		if (info->efr & IOMMU_EFR_PPR_SUP)
			flags |= IVHD_FLAG_PPE_SUP;
		if (info->efr & IOMMU_EFR_PREF_SUP)
			flags |= IVHD_FLAG_PREF_SUP;
	}
	if (info->control & IOMMU_CTRL_COHERENT)
		flags |= IVHD_FLAG_COHERENT;
	if (info->cap & IOMMU_CAP_IOTLB_SUP)
		flags |= IVHD_FLAG_IOTLB_SUP;
	if (info->control & IOMMU_CTRL_ISOC)
		flags |= IVHD_FLAG_ISOC;
	if (info->control & IOMMU_CTRL_RES_PASS_PW)
		flags |= IVHD_FLAG_RES_PASS_PW;
	if (info->control & IOMMU_CTRL_PASS_PW)
		flags |= IVHD_FLAG_PASS_PW;
	if (info->control & IOMMU_CTRL_HT_TUN_EN)
		flags |= IVHD_FLAG_HT_TUN_EN;

	return flags;
}

static u16 ivhd_info(const struct iommu_info *info)
{
	return FIELD_GET(IOMMU_MISC_MSI_NUM, info->misc) |
		FIELD_GET(IOMMU_RANGE_UNIT_ID, info->range) <<
		IVHD_INFO_UNIT_ID_SHIFT;
}

/* Write a type 10h block, which every OS understands */
static void *write_ivhd_legacy(void *p, const struct iommu_info *info)
{
	struct acpi_ivhd *ivhd = p;
	u32 feat;

	memset(ivhd, '\0', sizeof(*ivhd));
	ivhd->type = IVHD_TYPE_LEGACY;
	ivhd->flags = ivhd_flags(info, true);
	ivhd->device_id = IVHD_DEVID(info->busno, 0, 2);
	ivhd->cap_offset = IOMMU_CAP_HDR;
	ivhd->base = info->base;
	ivhd->info = ivhd_info(info);
	feat = FIELD_PREP(IVHD_FEAT_HATS, FIELD_GET(IOMMU_EFR_HATS, info->efr));
	feat |= FIELD_PREP(IVHD_FEAT_GATS,
			   FIELD_GET(IOMMU_EFR_GATS, info->efr));
	feat |= FIELD_PREP(IVHD_FEAT_MSI_NUM_PPR,
			   FIELD_GET(IOMMU_MISC_MSI_NUM_PPR, info->misc));
	feat |= FIELD_PREP(IVHD_FEAT_PN_BANKS,
			   FIELD_GET(IOMMU_CNT_BANKS, info->counter_cfg));
	feat |= FIELD_PREP(IVHD_FEAT_PN_COUNTERS,
			   FIELD_GET(IOMMU_CNT_COUNTERS, info->counter_cfg));
	feat |= FIELD_PREP(IVHD_FEAT_PAS_MAX,
			   FIELD_GET(IOMMU_EFR_PAS_MAX, info->efr));
	if (info->efr & IOMMU_EFR_HE_SUP)
		feat |= IVHD_FEAT_HE_SUP;
	if (info->efr & IOMMU_EFR_GA_SUP)
		feat |= IVHD_FEAT_GA_SUP;
	if (info->efr & IOMMU_EFR_IA_SUP)
		feat |= IVHD_FEAT_IA_SUP;
	feat |= FIELD_PREP(IVHD_FEAT_GLX, FIELD_GET(IOMMU_EFR_GLX, info->efr));
	if (info->efr & IOMMU_EFR_GT_SUP)
		feat |= IVHD_FEAT_GT_SUP;
	if (info->efr & IOMMU_EFR_NX_SUP)
		feat |= IVHD_FEAT_NX_SUP;
	if (info->efr & IOMMU_EFR_XT_SUP)
		feat |= IVHD_FEAT_XT_SUP;
	ivhd->feature = feat;

	p = add_devices(ivhd + 1, info->busno);
	ivhd->length = p - (void *)ivhd;

	return p;
}

/* Write a type 11h block, with the extended feature registers */
static void *write_ivhd_full(void *p, const struct iommu_info *info)
{
	struct acpi_ivhd_11 *ivhd = p;

	memset(ivhd, '\0', sizeof(*ivhd));
	ivhd->type = IVHD_TYPE_FULL;
	ivhd->flags = ivhd_flags(info, false);
	ivhd->device_id = IVHD_DEVID(info->busno, 0, 2);
	ivhd->cap_offset = IOMMU_CAP_HDR;
	ivhd->base = info->base;
	ivhd->info = ivhd_info(info);
	ivhd->attr = FIELD_PREP(IVHD_ATTR_MSI_NUM_PPR,
				FIELD_GET(IOMMU_MISC_MSI_NUM_PPR, info->misc)) |
		FIELD_PREP(IVHD_ATTR_PN_BANKS,
			   FIELD_GET(IOMMU_CNT_BANKS, info->counter_cfg)) |
		FIELD_PREP(IVHD_ATTR_PN_COUNTERS,
			   FIELD_GET(IOMMU_CNT_COUNTERS, info->counter_cfg));
	ivhd->efr = info->efr;
	ivhd->efr2 = info->efr2;

	p = add_devices(ivhd + 1, info->busno);
	ivhd->length = p - (void *)ivhd;

	return p;
}

static int turin_write_ivrs(struct acpi_ctx *ctx,
			    const struct acpi_writer *entry)
{
	struct acpi_ivrs *ivrs = ctx->current;
	struct acpi_table_header *header = &ivrs->header;
	void *p = ivrs + 1;
	int bus, count = 0;

	memset(ivrs, '\0', sizeof(*ivrs));
	for (bus = 0; bus < PCI_BUS_COUNT; bus += TURIN_BUSES_PER_ROOT) {
		struct iommu_info info;
		u32 base;

		if (turin_get_iommu(bus, &base))
			continue;
		iommu_read(&info, bus, base);
		if (!count) {
			ivrs->ivinfo = info.misc & IVRS_IVINFO_SIZE_MASK;
			if (info.cap & IOMMU_CAP_EFR_SUP)
				ivrs->ivinfo |= IVRS_IVINFO_EFR_SUP;
		}
		p = write_ivhd_legacy(p, &info);
		if (info.cap & IOMMU_CAP_EFR_SUP)
			p = write_ivhd_full(p, &info);
		count++;
	}
	if (!count) {
		log_debug("no IOMMUs\n");
		return 0;
	}

	acpi_fill_header(header, "IVRS");
	header->length = p - (void *)ivrs;
	header->revision = IVRS_FORMAT_FIXED;
	header->checksum = table_compute_checksum(ivrs, header->length);
	acpi_inc(ctx, header->length);
	log_debug("%d IOMMUs\n", count);

	return acpi_add_table(ctx, ivrs);
}
ACPI_WRITER(5ivrs, "IVRS", turin_write_ivrs, 0);
