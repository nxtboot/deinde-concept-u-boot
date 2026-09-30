// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * PCI host bridges on AMD EPYC Turin
 *
 * The processor has a root complex for each of its NBIO/IOHC blocks, so
 * there are several root buses, each owning a range of bus numbers. The data
 * fabric decides which root complex a configuration, I/O or MMIO transaction
 * goes to from its address, using the bus-number, I/O and MMIO maps in the
 * fabric's function-0 register space. Those are written through the
 * broadcast fabric device at 00:18.0, beyond the 256 bytes the legacy
 * 0xcf8 mechanism can reach, so ECAM is set up first.
 *
 * The PSP's ABL fills in the bus-number maps, so each root bus is already
 * reachable. It leaves the I/O and MMIO maps to the BIOS, which openSIL calls
 * its resource manager. Here each root bus is a devicetree node whose bus-range
 * gives its bus numbers and whose ranges give the windows it may hand out; this
 * driver programs the fabric maps from the ranges before the bus is enumerated,
 * so that the BARs U-Boot assigns are actually routed.
 */

#define LOG_CATEGORY UCLASS_PCI

#include <dm.h>
#include <log.h>
#include <pci.h>
#include <acpi/acpigen.h>
#include <asm/cpu.h>
#include <asm/arch/cpu.h>
#include <asm/arch/trace.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/pci.h>
#include <dm/acpi.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

/* ECAM: the fabric registers and the MSR must agree */
#define ECAM_BASE		((ulong)CONFIG_PCIE_ECAM_BASE)
#define ECAM_BUSES		256
#define ECAM_SIZE		(ECAM_BUSES * SZ_1M)
#define MSR_MMIO_CONF_BASE	0xc0010058
#define MMIO_CONF_EN		BIT(0)
#define MMIO_CONF_BUS_RANGE_SHIFT 2

/* SMN index/data pair in the root complex's config space */
#define SMN_INDEX		0xb8
#define SMN_DATA		0xbc

/* Data-fabric function 0 as seen on SMN; its ECAM window registers */
#define SMN_DF_F0_BASE		0x49000000
#define DF_MMCONF_BASE_LO	0xc10
#define DF_MMCONF_BASE_HI	0xc14
#define DF_MMCONF_LIMIT_LO	0xc18
#define DF_MMCONF_LIMIT_HI	0xc1c
#define DF_MMCONF_EN		BIT(0)

/* Data-fabric function 0 config space, reached through ECAM at 00:18.0 */
#define DF_BDF			PCI_BDF(0, 0x18, 0)
#define DF_REG(reg)		(ECAM_BASE + (PCI_DEV(DF_BDF) << 15) + (reg))

/* Address maps: one entry per root complex, selected by DstFabricID */
#define DF_MAP_RE		BIT(0)
#define DF_MAP_WE		BIT(1)

#define DF_NUM_CFG_MAPS		8
#define DF_CFG_BASE(i)		(0xc80 + (i) * 8)
#define DF_CFG_LIMIT(i)		(0xc84 + (i) * 8)
#define DF_CFG_BUS_SHIFT	16	/* base and limit bus numbers */
#define DF_CFG_BUS_MASK		0xff
#define DF_CFG_FID_MASK		0xff	/* in the limit register */

#define DF_NUM_IO_MAPS		8
#define DF_IO_BASE(i)		(0xd00 + (i) * 8)
#define DF_IO_LIMIT(i)		(0xd04 + (i) * 8)
#define DF_IO_ADDR_SHIFT	16	/* address bits 24:12 in bits 28:16 */
#define DF_IO_FID_MASK		0xff	/* in the limit register */

#define DF_NUM_MMIO_MAPS	16
#define DF_MMIO_BASE(i)		(0xd80 + (i) * 0x10)	/* address 47:16 */
#define DF_MMIO_LIMIT(i)	(0xd84 + (i) * 0x10)
#define DF_MMIO_CTRL(i)		(0xd88 + (i) * 0x10)
#define DF_MMIO_EXT(i)		(0xd8c + (i) * 0x10)	/* address 55:48 */
#define DF_MMIO_ADDR_SHIFT	16
#define DF_MMIO_CTRL_FID_SHIFT	16

/*
 * The IOS fabric IDs run from 0x20, alternating between the big IOHCs (even,
 * with an NBIF for the FCH's USB and SATA) and the small ones (odd). Each
 * IOHC's registers sit in their own SMN aperture, 1MB apart, as openSIL's
 * NBIO_SPACE() lays them out. The big IOHCs' apertures follow the fabric IDs
 * but the small ones' are swapped in pairs: fabric ID 0x21 has the second
 * aperture and 0x23 the first (the I/O APIC of each only answers at an
 * address within its own root complex's window)
 */
#define DF_FID_IOS_BASE		0x20
#define IOHC_BIG_BASE		0x13b10000
#define IOHC_SMALL_BASE		0x1d410000
#define IOHC_STRIDE		SZ_1M

/*
 * Where the IOHC sends transactions it cannot match to one of its own
 * devices: the southbridge (FCH) port. Only the primary root complex, bus 0
 * with the FCH's LPC bridge at 00:14.3, really has one; the ABL leaves every
 * IOHC pointing at the same port, so on the others a config read of an empty
 * slot never completes and the fabric's watchdog resets the machine a few
 * seconds later. openSIL clears it on the IOHCs without a southbridge
 * (NbioBaseConfigurationBrh). This must happen before anything reads an
 * empty slot on the bus, so it cannot be decided by looking for the FCH
 */
#define IOHC_NB_BUS_NUM_CNTL	0x44	/* the root complex's own bus number */
#define NB_BUS_LAT_MODE		BIT(8)
#define IOHC_SB_LOCATION	0x7c
/* the IOMMU's own copies of the southbridge location, in its L1 and L2 */
#define IOMMU_L1_SB_LOCATION	0x15300024
#define IOMMU_L2_SB_LOCATION	0x13f0112c

/*
 * Each IOHC has an I/O APIC for its PCIe INTx interrupts, which the ABL
 * leaves disabled, every one at 0xfec00000 with the enable bit clear.
 * openSIL gives each one 64KB of its root complex's MMIO window
 * (NbioIoApicMmioAddressBrh), an ID (NbioIoApicPreDefIdBrh) and sets the
 * southbridge and secondary features, plus 'no southbridge' on the others
 * (NbioIoapicInitBrh)
 */
#define IOHC_IOAPIC_BASE_LO	0x2f0
#define IOHC_IOAPIC_BASE_HI	0x2f4
#define IOAPIC_BASE_EN		BIT(0)
#define IOAPIC_BIG_FEATURES	0x14300000
#define IOAPIC_SMALL_FEATURES	0x1d800000
#define IOAPIC_FEAT_SB		BIT(2)
#define IOAPIC_FEAT_SECONDARY	BIT(4)
#define IOAPIC_FEAT_NO_SB	BIT(5)
#define IOAPIC_BIG_ID		0x02801000
#define IOAPIC_SMALL_ID		0x1d001000
#define IOAPIC_ID_BASE		0xf0
#define IOAPIC_ID_SHIFT		24
#define IOAPIC_MMIO_SIZE	SZ_64K

/*
 * The IOMMU is device 0.2 of a big root complex, with the base of its MMIO
 * registers in its capability at 0x40
 */
#define DEVFN(dev, func)	((dev) << 3 | (func))
#define IOMMU_DEVFN		DEVFN(0, 2)
#define IOMMU_CAP_BASE_LO	0x44
#define IOMMU_CAP_BASE_HI	0x48
#define IOMMU_BASE_EN		BIT(0)
#define IOMMU_MMIO_SIZE		SZ_512K

/*
 * Each bridge's INTA-D reach the root complex's I/O APIC through one of its
 * pin groups of four, swizzled first by the register's 'swz' field. The
 * registers follow the RCEC's, in bridge-control order. The IOHC's remap
 * registers give the device and function of each bridge
 */
#define IOAPIC_RCEC_ROUTING	0x3c
#define IOAPIC_BR_ROUTING	0x40
#define ROUTE(grp, swz, map)	((grp) | (swz) << 4 | (map) << 16)
#define RCEC_ROUTE		ROUTE(3, 0, 0)	/* as openSIL, on every IOHC */
#define ROUTE_GRP(val)		((val) & 7)
#define ROUTE_SWZ(val)		(((val) >> 4) & 3)
#define IOAPIC_PINS_PER_GRP	4
#define PCI_NUM_PINS		4
#define IOHC_DEV_REMAP		0xb8
#define IOHC_NUM_BRIDGES	24
#define IOHC_INTERNAL_BRIDGE	20
#define REMAP_DEVFN_MASK	0xff

/*
 * How much of a window above 4GB to map for U-Boot's own use: it allocates
 * BARs from the start of the window and needs far less than this
 */
#define HIGH_WINDOW_MAP_SIZE	SZ_4G

/*
 * Each big IOHC has an NBIF carrying the FCH-type functions (USB, SATA, the
 * crypto coprocessor, audio) as functions of an internal bridge. Only the
 * NBIFs of the first and last big IOHCs are wired to an FCH; the functions
 * on the other two exist but are inert, and their drivers would waste time
 * timing out on them. openSIL leaves only the dummy function 0, the DMA
 * function 1 and (where present) USB and SATA enabled, using the per-function
 * enable straps
 */
#define NBIF_STRAP_BASE		0x10134000
#define NBIF_STRAP_STRIDE	0x200	/* per function of port 0 */
#define NBIF_SATA_STRAP_BASE	0x10135000	/* port 1 functions 0 and 1 */
#define NBIF_STRAP_FUNC_EN	BIT(28)
/*
 * The NBIF's two root ports, devices 7.1 and 7.2 of the root bus, learn
 * their own bus, device and function from these straps, which requests
 * they originate or forward then carry as the requester ID
 */
#define NBIF_PORT_STRAP7	0x1013101c
#define NBIF_PORT_STRAP_STRIDE	0x200
#define NBIF2_OFFSET		0x400000
#define STRAP7_RP_BUSNUM_SHIFT	16
#define STRAP7_RP_BUSNUM_MASK	GENMASK(23, 16)
#define STRAP7_DN_DEVNUM_SHIFT	24
#define STRAP7_DN_DEVNUM_MASK	GENMASK(28, 24)
#define STRAP7_DN_FUNCID_SHIFT	29
#define STRAP7_DN_FUNCID_MASK	GENMASK(31, 29)
#define NBIF_PORT_DEV		7
/*
 * Which functions' interrupt lines the NBIF passes on: device 0 functions
 * 0, 1, 4 (USB) and 5, and device 1 functions 0 and 1 (SATA)
 */
#define NBIF_INTR_LINE_ENABLE	0x1013a008
#define NBIF_INTR_LINES		0x333
#define NBIF_FUNC_USB		4
#define NBIF_NUM_FUNCS		8
#define NBIF_NUM_SATA		2
#define IOHC_NUM_BIG		4

/*
 * The internal bridge to the SATA functions (07.2) on those IOHCs cannot be
 * emptied through the straps, so openSIL hides the bridge itself
 * (NbifSATAHideBridgeTbl) with the disable, config-disable and hide bits
 * of its bridge-control register in the IOHC's PCIe block
 */
#define IOHC_SATA_BRIDGE_CNTL	0x13b38404
#define BRIDGE_CNTL_HIDE	(BIT(18) | BIT(2) | BIT(0))

/*
 * Some root ports appear under two root complexes, e.g. the one for the slot
 * behind 20:01.1 also shows as e0:03.1, and a write to either reaches the
 * same port. openSIL hides the duplicates (PcieHideBridgeTbl and
 * PcieHideBridgePcie6Tbl): bridges 17-19 on every big IOHC and 9-16 on all
 * but the second, using the same bridge-control bits as for SATA
 */
#define IOHC_BRIDGE_CNTL(n)	(0x13b31004 + (n) * 0x400)
#define IOHC_HIDE_ALL_FIRST	17
#define IOHC_HIDE_ALL_LAST	19
#define IOHC_HIDE_FIRST		9
#define IOHC_HIDE_LAST		16
#define IOHC_NO_HIDE		1	/* big IOHC which keeps bridges 9-16 */

static u32 df_read(uint reg)
{
	return readl(DF_REG(reg));
}

static void df_write(uint reg, u32 val)
{
	writel(val, DF_REG(reg));
	turin_trace_pci(PCI_BDF(0, PCI_DEV(DF_BDF), reg >> 12), reg & 0xfff,
			val, 32);
}

static u32 smn_read(u32 reg)
{
	ulong val;

	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_read_config(PCI_BDF(0, 0, 0), SMN_DATA, &val, PCI_SIZE_32);

	return val;
}

static void smn_write(u32 reg, u32 val)
{
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_INDEX, reg, PCI_SIZE_32);
	pci_x86_write_config(PCI_BDF(0, 0, 0), SMN_DATA, val, PCI_SIZE_32);
	turin_trace_smn(reg, val);
}

/**
 * turin_ecam_init() - Move the ECAM window to where U-Boot can reach it
 *
 * The ABL puts the ECAM near the top of the 48-bit address space, beyond
 * U-Boot's page tables. This moves it just above the low DRAM, updating the
 * fabric's window before the MSR, in the order coreboot uses.
 */
void turin_ecam_init(void)
{
	u64 limit = ECAM_BASE + ECAM_SIZE - 1;

	if (smn_read(SMN_DF_F0_BASE + DF_MMCONF_BASE_LO) ==
	    (ECAM_BASE | DF_MMCONF_EN))
		return;

	/* the fabric window: disable, set the new limit and base, enable */
	smn_write(SMN_DF_F0_BASE + DF_MMCONF_BASE_LO,
		  smn_read(SMN_DF_F0_BASE + DF_MMCONF_BASE_LO) & ~DF_MMCONF_EN);
	smn_write(SMN_DF_F0_BASE + DF_MMCONF_LIMIT_HI, limit >> 32);
	smn_write(SMN_DF_F0_BASE + DF_MMCONF_LIMIT_LO,
		  (u32)limit & ~(SZ_1M - 1));
	smn_write(SMN_DF_F0_BASE + DF_MMCONF_BASE_HI, 0);
	smn_write(SMN_DF_F0_BASE + DF_MMCONF_BASE_LO, ECAM_BASE | DF_MMCONF_EN);

	wrmsrl(MSR_MMIO_CONF_BASE, ECAM_BASE | MMIO_CONF_EN |
	       (__fls(ECAM_BUSES) << MMIO_CONF_BUS_RANGE_SHIFT));
}

/**
 * turin_find_root() - Find the fabric's config map for a root bus
 *
 * @busno: Root bus number
 * @fidp: Returns the fabric ID of the root complex owning that bus
 * Return: index of the map, or -ENODEV if the bus is not mapped
 */
static int turin_find_root(int busno, uint *fidp)
{
	int i;

	for (i = 0; i < DF_NUM_CFG_MAPS; i++) {
		u32 base = df_read(DF_CFG_BASE(i));
		u32 limit = df_read(DF_CFG_LIMIT(i));

		if (!(base & DF_MAP_RE))
			continue;
		if (((base >> DF_CFG_BUS_SHIFT) & DF_CFG_BUS_MASK) == busno) {
			*fidp = limit & DF_CFG_FID_MASK;
			return i;
		}
	}

	return -ENODEV;
}

/* Get the index of an IOHC's SMN aperture among the big or small ones */
static uint turin_iohc_index(uint fid)
{
	uint n = (fid - DF_FID_IOS_BASE) >> 1;

	return (fid & 1) ? n ^ 1 : n;
}

/* openSIL numbers the big IOHCs 0-3 and the small ones 4-7 */
static uint turin_ioapic_id(uint fid)
{
	return IOAPIC_ID_BASE + ((fid & 1) ? IOHC_NUM_BIG : 0) +
		turin_iohc_index(fid);
}

static u32 turin_iohc_base(uint fid)
{
	return ((fid & 1) ? IOHC_SMALL_BASE : IOHC_BIG_BASE) +
		turin_iohc_index(fid) * IOHC_STRIDE;
}

/**
 * turin_iohc_init() - Set up a root complex's IOHC before enumerating it
 *
 * @busno: Root bus number
 * @fid: Fabric ID of its IOS
 */
/* Tell an NBIF's root ports which bus, device and function they are */
static void turin_nbif_strap_ports(u32 base, int busno)
{
	int port;

	for (port = 0; port < 2; port++) {
		u32 reg = base + port * NBIF_PORT_STRAP_STRIDE;
		u32 val = smn_read(reg);

		if (val == ~0U)
			return;		/* no such NBIF */
		val &= ~(STRAP7_RP_BUSNUM_MASK | STRAP7_DN_DEVNUM_MASK |
			 STRAP7_DN_FUNCID_MASK);
		val |= busno << STRAP7_RP_BUSNUM_SHIFT |
			NBIF_PORT_DEV << STRAP7_DN_DEVNUM_SHIFT |
			(port + 1) << STRAP7_DN_FUNCID_SHIFT;
		smn_write(reg, val);
	}
}

static void turin_nbif_init(int busno, uint fid)
{
	uint n = (fid - DF_FID_IOS_BASE) >> 1;
	u32 base = NBIF_STRAP_BASE + n * IOHC_STRIDE;
	bool has_fch = !n || n == IOHC_NUM_BIG - 1;
	int i;

	if (fid & 1)
		return;
	turin_nbif_strap_ports(NBIF_PORT_STRAP7 + n * IOHC_STRIDE, busno);
	turin_nbif_strap_ports(NBIF_PORT_STRAP7 + NBIF2_OFFSET + n * IOHC_STRIDE,
			       busno);
	/* let the functions' interrupts through, which the ABL leaves off */
	smn_write(NBIF_INTR_LINE_ENABLE + n * IOHC_STRIDE, NBIF_INTR_LINES);
	for (i = 2; i < NBIF_NUM_FUNCS; i++) {
		if (i == NBIF_FUNC_USB && has_fch)
			continue;
		smn_write(base + i * NBIF_STRAP_STRIDE,
			  smn_read(base + i * NBIF_STRAP_STRIDE) &
			  ~NBIF_STRAP_FUNC_EN);
	}
	if (has_fch)
		return;
	base = NBIF_SATA_STRAP_BASE + n * IOHC_STRIDE;
	for (i = 0; i < NBIF_NUM_SATA; i++)
		smn_write(base + i * NBIF_STRAP_STRIDE,
			  smn_read(base + i * NBIF_STRAP_STRIDE) &
			  ~NBIF_STRAP_FUNC_EN);
	base = IOHC_SATA_BRIDGE_CNTL + n * IOHC_STRIDE;
	smn_write(base, smn_read(base) | BRIDGE_CNTL_HIDE);
}

/*
 * The root ports behind each IOHC, in the order of their bridge-control
 * registers (openSIL's DefaultPortDevMap). Bit 18 of the register enables
 * configuration-retry (CRS) handling, which openSIL sets for every port
 */

static const u8 turin_port_devfn[] = {
	DEVFN(1, 1), DEVFN(1, 2), DEVFN(1, 3), DEVFN(1, 4), DEVFN(1, 5),
	DEVFN(1, 6), DEVFN(1, 7), DEVFN(2, 1), DEVFN(2, 2), DEVFN(3, 1),
	DEVFN(3, 2), DEVFN(3, 3), DEVFN(3, 4), DEVFN(3, 5), DEVFN(3, 6),
	DEVFN(3, 7), DEVFN(4, 1),
};

/*
 * The I/O APIC pin group and swizzle of each bridge, as openSIL programs
 * them on this board (traced from its NbioIoapicIntrRoutingTbl). The
 * bridges spread over seven groups, so no two of a root complex's ports
 * share a pin. A small IOHC has only the first nine bridges
 */
static const u32 turin_bridge_routing[] = {
	ROUTE(0, 0, 0), ROUTE(1, 0, 0), ROUTE(2, 0, 0), ROUTE(3, 0, 0),
	ROUTE(4, 0, 0), ROUTE(5, 0, 0), ROUTE(6, 0, 0), ROUTE(6, 2, 0),
	ROUTE(5, 2, 0), ROUTE(4, 2, 1), ROUTE(3, 2, 1), ROUTE(2, 2, 1),
	ROUTE(1, 2, 1), ROUTE(0, 2, 1), ROUTE(0, 1, 1), ROUTE(1, 1, 1),
	ROUTE(2, 1, 1), ROUTE(3, 1, 1), ROUTE(4, 1, 2), ROUTE(5, 1, 2),
};
#define SMALL_IOHC_BRIDGES	9

#define IOHC_BRIDGE_CNTL_OFF	0x21004
#define BRIDGE_CNTL_CRS_EN	BIT(18)
#define BRIDGE_CNTL_HIDE_BITS	(BIT(2) | BIT(0))

static void turin_hide_bridge(u32 base, int n)
{
	u32 reg = base + IOHC_BRIDGE_CNTL(n);

	smn_write(reg, smn_read(reg) | BRIDGE_CNTL_HIDE);
}

static void turin_hide_dup_bridges(uint fid)
{
	uint n = (fid - DF_FID_IOS_BASE) >> 1;
	u32 offset = n * IOHC_STRIDE;
	int i;

	if (fid & 1)
		return;
	for (i = IOHC_HIDE_ALL_FIRST; i <= IOHC_HIDE_ALL_LAST; i++)
		turin_hide_bridge(offset, i);
	if (n == IOHC_NO_HIDE)
		return;
	for (i = IOHC_HIDE_FIRST; i <= IOHC_HIDE_LAST; i++)
		turin_hide_bridge(offset, i);
}

/**
 * turin_ioapic_init() - Enable a root complex's I/O APIC
 *
 * @busno: Root bus number
 * @fid: Fabric ID of its IOS
 * @addr: MMIO address to give it
 */
/* Get the SMN address of a root complex's I/O APIC registers */
static u32 turin_ioapic_regs(uint fid)
{
	return ((fid & 1) ? IOAPIC_SMALL_FEATURES : IOAPIC_BIG_FEATURES) +
		turin_iohc_index(fid) * IOHC_STRIDE;
}

/*
 * Route each bridge's INTA-D, and the root complex event collector's
 * interrupt, to the I/O APIC pins the OS is told about
 */
static void turin_route_bridges(uint fid, u32 regs)
{
	int i, count;

	smn_write(regs + IOAPIC_RCEC_ROUTING, RCEC_ROUTE);
	count = (fid & 1) ? SMALL_IOHC_BRIDGES : ARRAY_SIZE(turin_bridge_routing);
	for (i = 0; i < count; i++)
		smn_write(regs + IOAPIC_BR_ROUTING + i * 4,
			  turin_bridge_routing[i]);
}

static void turin_ioapic_init(int busno, uint fid, u32 addr)
{
	u32 offset = turin_iohc_index(fid) * IOHC_STRIDE;
	u32 iohc = turin_iohc_base(fid);
	u32 feat, id;

	feat = turin_ioapic_regs(fid);
	id = ((fid & 1) ? IOAPIC_SMALL_ID : IOAPIC_BIG_ID) + offset;
	turin_route_bridges(fid, feat);
	smn_write(feat, smn_read(feat) | IOAPIC_FEAT_SB |
		  IOAPIC_FEAT_SECONDARY |
		  (busno != FCH_ROOT_BUS ? IOAPIC_FEAT_NO_SB : 0));
	smn_write(id, turin_ioapic_id(fid) << IOAPIC_ID_SHIFT);
	smn_write(iohc + IOHC_IOAPIC_BASE_HI, 0);
	smn_write(iohc + IOHC_IOAPIC_BASE_LO, addr | IOAPIC_BASE_EN);
	log_debug("bus %x: I/O APIC %x at %x\n", busno, turin_ioapic_id(fid),
		  addr);
}

/* Read a root port's configuration space through the ECAM */
static void *ecam_addr(int busno, uint devfn, uint offset)
{
	return (void *)(ECAM_BASE + ((ulong)busno << 20) + (devfn << 12) +
			offset);
}

static u16 port_read16(int busno, uint devfn, uint offset)
{
	return readw(ecam_addr(busno, devfn, offset));
}

/* Put a big root complex's IOMMU registers at @addr */
static void turin_iommu_init(int busno, u32 addr)
{
	if (readw(ecam_addr(busno, IOMMU_DEVFN, PCI_VENDOR_ID)) == 0xffff) {
		log_debug("bus %x: no IOMMU\n", busno);
		return;
	}
	writel(0, ecam_addr(busno, IOMMU_DEVFN, IOMMU_CAP_BASE_HI));
	turin_trace_pci(PCI_BDF(busno, 0, 2), IOMMU_CAP_BASE_HI, 0, 32);
	writel(addr | IOMMU_BASE_EN,
	       ecam_addr(busno, IOMMU_DEVFN, IOMMU_CAP_BASE_LO));
	turin_trace_pci(PCI_BDF(busno, 0, 2), IOMMU_CAP_BASE_LO,
			addr | IOMMU_BASE_EN, 32);
	log_debug("bus %x: IOMMU at %x\n", busno, addr);
}

int turin_get_iommu(int busno, u32 *basep)
{
	uint fid;
	u32 val;
	int ret;

	ret = turin_find_root(busno, &fid);
	if (ret < 0)
		return ret;
	if (fid & 1)
		return -ENOENT;
	if (readw(ecam_addr(busno, IOMMU_DEVFN, PCI_VENDOR_ID)) == 0xffff)
		return -ENOENT;
	val = readl(ecam_addr(busno, IOMMU_DEVFN, IOMMU_CAP_BASE_LO));
	if (!(val & IOMMU_BASE_EN))
		return -ENOENT;
	*basep = val & ~IOMMU_BASE_EN;

	return 0;
}

int turin_get_paired_bus(int busno)
{
	uint fid, other;
	int bus, ret;

	ret = turin_find_root(busno, &fid);
	if (ret < 0)
		return ret;
	for (bus = 0; bus < PCI_BUS_COUNT; bus += TURIN_BUSES_PER_ROOT) {
		if (turin_find_root(bus, &other) < 0 || !(other & 1))
			continue;
		if (turin_iohc_index(other) == turin_iohc_index(fid))
			return bus;
	}

	return -ENOENT;
}

/*
 * turin_port_in_use() - Check whether a root port has something behind it
 *
 * A port is in use if its link is up, or its slot has a card present or
 * supports hot-plug, which openSIL also keeps visible
 */
static bool turin_port_in_use(int busno, uint devfn)
{
	uint pos, ttl = 48;
	u16 flags, val;

	if (port_read16(busno, devfn, PCI_VENDOR_ID) == 0xffff)
		return false;
	if (!(port_read16(busno, devfn, PCI_STATUS) & PCI_STATUS_CAP_LIST))
		return false;
	pos = port_read16(busno, devfn, PCI_CAPABILITY_LIST) & 0xfc;
	while (pos && ttl--) {
		val = port_read16(busno, devfn, pos);
		if ((val & 0xff) == PCI_CAP_ID_EXP)
			break;
		pos = (val >> 8) & 0xfc;
	}
	if (!pos || !ttl)
		return false;
	if (port_read16(busno, devfn, pos + PCI_EXP_LNKSTA) & PCI_EXP_LNKSTA_DLLLA)
		return true;
	flags = port_read16(busno, devfn, pos + PCI_EXP_FLAGS);
	if (!(flags & PCI_EXP_FLAGS_SLOT))
		return false;
	if (readl(ECAM_BASE + ((ulong)busno << 20) + (devfn << 12) + pos +
		  PCI_EXP_SLTCAP) & PCI_EXP_SLTCAP_HPC)
		return true;

	return port_read16(busno, devfn, pos + PCI_EXP_SLTSTA) &
		PCI_EXP_SLTSTA_PDS;
}

/*
 * turin_hide_unused_ports() - Hide the root ports with nothing behind them
 *
 * openSIL (MpioVisibilityControl) hides every root port after training the
 * links and then shows those in use. Without this, each unused port is
 * given a share of the root complex's windows, which is then not available
 * to devices with large BARs
 */
static void turin_hide_unused_ports(int busno, uint fid)
{
	u32 base = turin_iohc_base(fid) + IOHC_BRIDGE_CNTL_OFF;
	int i, hidden = 0;

	for (i = 0; i < ARRAY_SIZE(turin_port_devfn); i++) {
		u32 reg = base + i * 0x400;
		u32 ctrl = smn_read(reg) | BRIDGE_CNTL_CRS_EN;

		/* leave hidden ports, e.g. the duplicates, alone */
		if (!(ctrl & BRIDGE_CNTL_HIDE_BITS) &&
		    !turin_port_in_use(busno, turin_port_devfn[i])) {
			ctrl |= BRIDGE_CNTL_HIDE_BITS;
			hidden++;
		}
		smn_write(reg, ctrl);
	}
	log_debug("bus %x: hid %d unused root ports\n", busno, hidden);
}

static void turin_iohc_init(int busno, uint fid)
{
	/*
	 * Requests the root complex originates itself, such as its root
	 * ports' and I/O APIC's interrupt messages, carry this bus number as
	 * their requester ID. Without it they claim to be from bus 0, which
	 * the IOMMU rejects
	 */
	smn_write(turin_iohc_base(fid) + IOHC_NB_BUS_NUM_CNTL,
		  busno | NB_BUS_LAT_MODE);
	if (busno != FCH_ROOT_BUS) {
		log_debug("bus %x: no southbridge, clearing SB location\n",
			  busno);
		smn_write(turin_iohc_base(fid) + IOHC_SB_LOCATION, 0);
		/*
		 * The IOMMU has its own copies, which the ABL leaves set on
		 * every big root complex. They make the IOMMU take requests
		 * from that port as the southbridge's, i.e. from bus 0
		 */
		if (!(fid & 1)) {
			u32 off = turin_iohc_index(fid) * IOHC_STRIDE;

			smn_write(IOMMU_L1_SB_LOCATION + off, 0);
			smn_write(IOMMU_L2_SB_LOCATION + off, 0);
		}
	}
	turin_nbif_init(busno, fid);
	turin_hide_dup_bridges(fid);
	turin_hide_unused_ports(busno, fid);
}

int turin_get_ioapic(int busno, u32 *addrp, uint *idp)
{
	uint fid;
	u32 val;
	int ret;

	ret = turin_find_root(busno, &fid);
	if (ret < 0)
		return ret;
	val = smn_read(turin_iohc_base(fid) + IOHC_IOAPIC_BASE_LO);
	if (!(val & IOAPIC_BASE_EN))
		return -ENOENT;
	*addrp = val & ~IOAPIC_BASE_EN;
	*idp = turin_ioapic_id(fid);

	return 0;
}

int turin_gsi_base(int busno)
{
	int bus, gsi = FCH_IOAPIC_PINS;

	for (bus = 0; bus < busno; bus += TURIN_BUSES_PER_ROOT) {
		u32 addr;
		uint id;

		if (!turin_get_ioapic(bus, &addr, &id))
			gsi += NBIO_IOAPIC_PINS;
	}

	return gsi;
}

static void turin_set_io_map(int slot, uint fid, const struct pci_region *reg)
{
	ulong start = reg->phys_start;
	ulong end = start + reg->size - 1;

	log_debug("I/O map %d: %lx-%lx -> fabric %x\n", slot, start, end, fid);
	df_write(DF_IO_LIMIT(slot), ((end >> 12) << DF_IO_ADDR_SHIFT) | fid);
	df_write(DF_IO_BASE(slot), ((start >> 12) << DF_IO_ADDR_SHIFT) |
		 DF_MAP_RE | DF_MAP_WE);
}

static void turin_set_mmio_map(int slot, uint fid,
			       const struct pci_region *reg)
{
	u64 start = reg->phys_start;
	u64 end = start + reg->size - 1;

	log_debug("MMIO map %d: %llx-%llx -> fabric %x\n", slot, start, end,
		  fid);
	df_write(DF_MMIO_CTRL(slot), 0);
	df_write(DF_MMIO_LIMIT(slot), end >> DF_MMIO_ADDR_SHIFT);
	df_write(DF_MMIO_BASE(slot), start >> DF_MMIO_ADDR_SHIFT);
	df_write(DF_MMIO_EXT(slot), ((end >> 48) << 16) | (start >> 48));
	df_write(DF_MMIO_CTRL(slot), (fid << DF_MMIO_CTRL_FID_SHIFT) |
		 DF_MAP_RE | DF_MAP_WE);
}

/**
 * turin_pci_probe() - Route this root bus's windows to its root complex
 *
 * The uclass has already decoded the node's ranges into the controller's
 * regions. Each root complex gets one I/O map and up to two MMIO maps
 * (typically one below 4GB and one above), in the slots matching its
 * config map, which is how openSIL lays them out too.
 */
static int turin_pci_probe(struct udevice *bus)
{
	struct pci_controller *hose = dev_get_uclass_priv(bus);
	int busno = dev_seq(bus);
	int slot, mmio_slot;
	bool have_io = false, have_ioapic = false;
	uint fid;
	int i;

	turin_ecam_init();
	slot = turin_find_root(busno, &fid);
	if (slot < 0) {
		log_err("Root bus %x is not mapped by the fabric\n", busno);
		return log_msg_ret("cfg", slot);
	}
	log_debug("bus %x: config map %d, fabric %x\n", busno, slot, fid);
	turin_iohc_init(busno, fid);

	mmio_slot = slot;
	for (i = 0; i < hose->region_count; i++) {
		struct pci_region *reg = &hose->regions[i];

		if (reg->flags & PCI_REGION_SYS_MEMORY)
			continue;
		if ((reg->flags & PCI_REGION_TYPE) == PCI_REGION_IO) {
			if (have_io)
				return log_msg_ret("io", -E2BIG);
			turin_set_io_map(slot, fid, reg);
			have_io = true;
		} else {
			if (mmio_slot >= DF_NUM_MMIO_MAPS)
				return log_msg_ret("mmio", -E2BIG);
			turin_set_mmio_map(mmio_slot, fid, reg);
			mmio_slot += DF_NUM_CFG_MAPS;

			/* let U-Boot reach the BARs it puts above 4GB */
			if (reg->phys_start >= SZ_4G) {
				int ret;

				ret = x86_64_map_mmio(reg->phys_start,
						      min_t(u64, reg->size,
							    HIGH_WINDOW_MAP_SIZE));
				if (ret)
					return log_msg_ret("map", ret);
			}

			/*
			 * put the I/O APIC at the top of the first window,
			 * which the fabric map covers, and keep it from PCI
			 */
			if (!have_ioapic && reg->size > IOAPIC_MMIO_SIZE) {
				reg->size -= IOAPIC_MMIO_SIZE;
				turin_ioapic_init(busno, fid,
						  reg->bus_start + reg->size);
				have_ioapic = true;

				/* a big root complex's IOMMU goes below it */
				if (!(fid & 1) && reg->size > IOMMU_MMIO_SIZE) {
					reg->size -= IOMMU_MMIO_SIZE;
					turin_iommu_init(busno, reg->bus_start +
							 reg->size);
				}
			}
		}
	}

	return 0;
}

static int turin_pci_read_config(const struct udevice *bus, pci_dev_t bdf,
				 uint offset, ulong *valuep,
				 enum pci_size_t size)
{
	return pci_x86_read_config(bdf, offset, valuep, size);
}

static int turin_pci_write_config(struct udevice *bus, pci_dev_t bdf,
				  uint offset, ulong value,
				  enum pci_size_t size)
{
	turin_trace_pci(bdf, offset, value, 8 << size);

	return pci_x86_write_config(bdf, offset, value, size);
}

static const struct dm_pci_ops turin_pci_ops = {
	.read_config	= turin_pci_read_config,
	.write_config	= turin_pci_write_config,
};

/*
 * Find a bridge's index into the bridge-control and routing registers. The
 * remap registers give the PCIe ports' devfns but are zero for the internal
 * bridges 20 and 21, which are devices 7.1 and 7.2 (the USB and SATA
 * controllers behind them)
 */
static int turin_bridge_index(uint fid, uint devfn)
{
	u32 base = turin_iohc_base(fid) + IOHC_DEV_REMAP;
	int i;

	for (i = 0; i < IOHC_NUM_BRIDGES; i++) {
		if ((smn_read(base + i * 4) & REMAP_DEVFN_MASK) == devfn)
			return i;
	}
	if (devfn == DEVFN(7, 1) || devfn == DEVFN(7, 2))
		return IOHC_INTERNAL_BRIDGE + (devfn & 7) - 1;

	return -ENOENT;
}

/*
 * Write a bridge's _PRT: the device behind it has its INTA-D swizzled and
 * routed to the group of four pins in @route
 */
static void turin_write_prt(struct acpi_ctx *ctx, int gsi_base, u32 route)
{
	int pin;

	acpigen_write_name(ctx, "_PRT");
	acpigen_write_package(ctx, PCI_NUM_PINS);
	for (pin = 0; pin < PCI_NUM_PINS; pin++) {
		acpigen_write_package(ctx, 4);
		acpigen_write_dword(ctx, 0xffff);	/* device 0, any function */
		acpigen_write_byte(ctx, pin);
		acpigen_write_zero(ctx);		/* a GSI, not a link device */
		acpigen_write_dword(ctx, gsi_base +
				    ROUTE_GRP(route) * IOAPIC_PINS_PER_GRP +
				    (pin + ROUTE_SWZ(route)) % PCI_NUM_PINS);
		acpigen_pop_len(ctx);
	}
	acpigen_pop_len(ctx);
}

/*
 * Describe the interrupt routing of a root bus's own device 0 (the IOMMU
 * and the event collector, which share the collector's routing) and of
 * each bridge on it, so that the OS can find the I/O APIC pin of a
 * device's INTx. The root bus's own device is in the DSDT, so this adds its
 * _PRT and a device for each bridge to its scope
 */
static int turin_pci_fill_ssdt(const struct udevice *bus, struct acpi_ctx *ctx)
{
	int busno = dev_seq(bus);
	struct udevice *dev;
	char scope[16], name[8];
	int gsi_base, ret;
	u32 regs;
	uint fid;

	ret = turin_find_root(busno, &fid);
	if (ret < 0)
		return log_msg_ret("fid", ret);
	regs = turin_ioapic_regs(fid);
	gsi_base = turin_gsi_base(busno);
	snprintf(scope, sizeof(scope), "\\_SB.PC%02X", busno);
	acpigen_write_scope(ctx, scope);

	/* the root bus's own device 0, routed with the event collector */
	turin_write_prt(ctx, gsi_base, smn_read(regs + IOAPIC_RCEC_ROUTING));
	device_foreach_child(dev, bus) {
		struct pci_child_plat *plat = dev_get_parent_plat(dev);
		uint devfn = DEVFN(PCI_DEV(plat->devfn), PCI_FUNC(plat->devfn));
		int idx;

		if (plat->class >> 8 != PCI_CLASS_BRIDGE_PCI)
			continue;
		idx = turin_bridge_index(fid, devfn);
		if (idx < 0) {
			log_warning("bus %x: no routing for bridge %x.%x\n",
				    busno, PCI_DEV(plat->devfn),
				    PCI_FUNC(plat->devfn));
			continue;
		}
		snprintf(name, sizeof(name), "GP%02X", devfn);
		acpigen_write_device(ctx, name);
		acpigen_write_name_integer(ctx, "_ADR",
					   PCI_DEV(plat->devfn) << 16 |
					   PCI_FUNC(plat->devfn));
		turin_write_prt(ctx, gsi_base,
				smn_read(regs + IOAPIC_BR_ROUTING + idx * 4));
		acpigen_pop_len(ctx);	/* Device */
	}
	acpigen_pop_len(ctx);	/* Scope */

	return 0;
}

static const struct acpi_ops turin_pci_acpi_ops = {
	.fill_ssdt	= turin_pci_fill_ssdt,
};

static const struct udevice_id turin_pci_ids[] = {
	{ .compatible = "amd,turin-pci" },
	{ }
};

U_BOOT_DRIVER(turin_pci) = {
	.name	= "turin_pci",
	.id	= UCLASS_PCI,
	.of_match = turin_pci_ids,
	.ops	= &turin_pci_ops,
	.probe	= turin_pci_probe,
	ACPI_OPS_PTR(&turin_pci_acpi_ops)
};
