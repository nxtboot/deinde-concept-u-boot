// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2015 Google, Inc
 */

#include <dm.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/sandbox_pci.h>
#include <asm/test.h>
#include <dm/device-internal.h>
#include <dm/test.h>
#include <dm/uclass-internal.h>
#include <test/test.h>
#include <test/ut.h>

DECLARE_GLOBAL_DATA_PTR;

/* Test that sandbox PCI works correctly */
static int dm_test_pci_base(struct unit_test_state *uts)
{
	struct udevice *bus;

	ut_assertok(uclass_get_device(UCLASS_PCI, 0, &bus));

	return 0;
}
DM_TEST(dm_test_pci_base, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that sandbox PCI bus numbering and device works correctly */
static int dm_test_pci_busdev(struct unit_test_state *uts)
{
	struct udevice *bus;
	struct udevice *swap;
	u16 vendor, device;

	/* Test bus#0 and its devices */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0, &bus));

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x00, 0), &swap));
	vendor = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_VENDOR_ID, &vendor));
	ut_asserteq(SANDBOX_PCI_VENDOR_ID, vendor);
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &swap));
	device = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_DEVICE_ID, &device));
	ut_asserteq(SANDBOX_PCI_SWAP_CASE_EMUL_ID, device);

	/* Test bus#1 and its devices */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 1, &bus));

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &swap));
	vendor = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_VENDOR_ID, &vendor));
	ut_asserteq(SANDBOX_PCI_VENDOR_ID, vendor);
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x0c, 0), &swap));
	device = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_DEVICE_ID, &device));
	ut_asserteq(SANDBOX_PCI_SWAP_CASE_EMUL_ID, device);

	return 0;
}
DM_TEST(dm_test_pci_busdev, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that we can use the swapcase device correctly */
static int dm_test_pci_swapcase(struct unit_test_state *uts)
{
	struct udevice *swap;
	ulong io_addr, mem_addr;
	char *ptr;

	/* Check that asking for the device 0 automatically fires up PCI */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x00, 0), &swap));

	/* First test I/O */
	io_addr = dm_pci_read_bar32(swap, 0);
	outb(2, io_addr);
	ut_asserteq(2, inb(io_addr));

	/*
	 * Now test memory mapping - note we must unmap and remap to cause
	 * the swapcase emulation to see our data and response.
	 */
	mem_addr = dm_pci_read_bar32(swap, 1);
	ptr = map_sysmem(mem_addr, 20);
	strcpy(ptr, "This is a TesT");
	unmap_sysmem(ptr);

	ptr = map_sysmem(mem_addr, 20);
	ut_asserteq_str("tHIS IS A tESt", ptr);
	unmap_sysmem(ptr);

	/* Check that asking for the device 1 automatically fires up PCI */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &swap));

	/* First test I/O */
	io_addr = dm_pci_read_bar32(swap, 0);
	outb(2, io_addr);
	ut_asserteq(2, inb(io_addr));

	/*
	 * Now test memory mapping - note we must unmap and remap to cause
	 * the swapcase emulation to see our data and response.
	 */
	mem_addr = dm_pci_read_bar32(swap, 1);
	ptr = map_sysmem(mem_addr, 20);
	strcpy(ptr, "This is a TesT");
	unmap_sysmem(ptr);

	ptr = map_sysmem(mem_addr, 20);
	ut_asserteq_str("tHIS IS A tESt", ptr);
	unmap_sysmem(ptr);

	return 0;
}
DM_TEST(dm_test_pci_swapcase, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that we can dynamically bind the device driver correctly */
static int dm_test_pci_drvdata(struct unit_test_state *uts)
{
	struct udevice *bus, *swap;

	/* Check that asking for the device automatically fires up PCI */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 1, &bus));

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &swap));
	ut_asserteq(SWAP_CASE_DRV_DATA, swap->driver_data);
	ut_assertok(dev_has_ofnode(swap));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x0c, 0), &swap));
	ut_asserteq(SWAP_CASE_DRV_DATA, swap->driver_data);
	ut_assertok(dev_has_ofnode(swap));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x10, 0), &swap));
	ut_asserteq(SWAP_CASE_DRV_DATA, swap->driver_data);
	ut_assertok(!dev_has_ofnode(swap));

	return 0;
}
DM_TEST(dm_test_pci_drvdata, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test that devices on PCI bus#2 can be accessed correctly */
static int dm_test_pci_mixed(struct unit_test_state *uts)
{
	/* PCI bus#2 has both statically and dynamic declared devices */
	struct udevice *bus, *swap;
	u16 vendor, device;
	ulong io_addr, mem_addr;
	char *ptr;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 2, &bus));

	/* Test the dynamic device */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(2, 0x08, 0), &swap));
	vendor = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_VENDOR_ID, &vendor));
	ut_asserteq(SANDBOX_PCI_VENDOR_ID, vendor);

	/* First test I/O */
	io_addr = dm_pci_read_bar32(swap, 0);
	outb(2, io_addr);
	ut_asserteq(2, inb(io_addr));

	/*
	 * Now test memory mapping - note we must unmap and remap to cause
	 * the swapcase emulation to see our data and response.
	 */
	mem_addr = dm_pci_read_bar32(swap, 1);
	ptr = map_sysmem(mem_addr, 30);
	strcpy(ptr, "This is a TesT oN dYNAMIc");
	unmap_sysmem(ptr);

	ptr = map_sysmem(mem_addr, 30);
	ut_asserteq_str("tHIS IS A tESt On DynamiC", ptr);
	unmap_sysmem(ptr);

	/* Test the static device */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(2, 0x1f, 0), &swap));
	device = 0;
	ut_assertok(dm_pci_read_config16(swap, PCI_DEVICE_ID, &device));
	ut_asserteq(SANDBOX_PCI_SWAP_CASE_EMUL_ID, device);

	/* First test I/O */
	io_addr = dm_pci_read_bar32(swap, 0);
	outb(2, io_addr);
	ut_asserteq(2, inb(io_addr));

	/*
	 * Now test memory mapping - note we must unmap and remap to cause
	 * the swapcase emulation to see our data and response.
	 */
	mem_addr = dm_pci_read_bar32(swap, 1);
	ptr = map_sysmem(mem_addr, 30);
	strcpy(ptr, "This is a TesT oN sTATIc");
	unmap_sysmem(ptr);

	ptr = map_sysmem(mem_addr, 30);
	ut_asserteq_str("tHIS IS A tESt On StatiC", ptr);
	unmap_sysmem(ptr);

	return 0;
}
DM_TEST(dm_test_pci_mixed, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test looking up PCI capability and extended capability */
static int dm_test_pci_cap(struct unit_test_state *uts)
{
	struct udevice *bus, *swap;
	int cap;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0, &bus));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &swap));

	/* look up PCI_CAP_ID_EXP */
	cap = dm_pci_find_capability(swap, PCI_CAP_ID_EXP);
	ut_asserteq(PCI_CAP_ID_EXP_OFFSET, cap);

	/* look up PCI_CAP_ID_PCIX */
	cap = dm_pci_find_capability(swap, PCI_CAP_ID_PCIX);
	ut_asserteq(0, cap);

	/* look up PCI_CAP_ID_MSIX starting from PCI_CAP_ID_PM_OFFSET */
	cap = dm_pci_find_next_capability(swap, PCI_CAP_ID_PM_OFFSET,
					  PCI_CAP_ID_MSIX);
	ut_asserteq(PCI_CAP_ID_MSIX_OFFSET, cap);

	/* look up PCI_CAP_ID_VNDR starting from PCI_CAP_ID_EXP_OFFSET */
	cap = dm_pci_find_next_capability(swap, PCI_CAP_ID_EXP_OFFSET,
					  PCI_CAP_ID_VNDR);
	ut_asserteq(0, cap);

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 1, &bus));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &swap));

	/* look up PCI_EXT_CAP_ID_DSN */
	cap = dm_pci_find_ext_capability(swap, PCI_EXT_CAP_ID_DSN);
	ut_asserteq(PCI_EXT_CAP_ID_DSN_OFFSET, cap);

	/* look up PCI_EXT_CAP_ID_SRIOV */
	cap = dm_pci_find_ext_capability(swap, PCI_EXT_CAP_ID_SRIOV);
	ut_asserteq(0, cap);

	/* look up PCI_EXT_CAP_ID_DSN starting from PCI_EXT_CAP_ID_ERR_OFFSET */
	cap = dm_pci_find_next_ext_capability(swap, PCI_EXT_CAP_ID_ERR_OFFSET,
					      PCI_EXT_CAP_ID_DSN);
	ut_asserteq(PCI_EXT_CAP_ID_DSN_OFFSET, cap);

	/* look up PCI_EXT_CAP_ID_RCRB starting from PCI_EXT_CAP_ID_VC_OFFSET */
	cap = dm_pci_find_next_ext_capability(swap, PCI_EXT_CAP_ID_VC_OFFSET,
					      PCI_EXT_CAP_ID_RCRB);
	ut_asserteq(0, cap);

	return 0;
}
DM_TEST(dm_test_pci_cap, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test looking up BARs in EA capability structure */
static int dm_test_pci_ea(struct unit_test_state *uts)
{
	struct udevice *bus, *swap;
	void *bar;
	int cap;

	/*
	 * use emulated device mapping function, we're not using real physical
	 * addresses in this test
	 */
	sandbox_set_enable_pci_map(true);

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0, &bus));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x01, 0), &swap));

	/* look up PCI_CAP_ID_EA */
	cap = dm_pci_find_capability(swap, PCI_CAP_ID_EA);
	ut_asserteq(PCI_CAP_ID_EA_OFFSET, cap);

	/* test swap case in BAR 1 */
	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_0, 0, 4, PCI_REGION_TYPE, 0);
	ut_assertnonnull(bar);
	*(int *)bar = 2; /* swap upper/lower */

	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_1, 0, 0xff, PCI_REGION_TYPE,
			     0);
	ut_assertnonnull(bar);
	strcpy(bar, "ea TEST");
	unmap_sysmem(bar);
	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_1, 0, 0xff, PCI_REGION_TYPE,
			     0);
	ut_assertnonnull(bar);
	ut_asserteq_str("EA test", bar);

	/* test magic values in BARs2, 4;  BAR 3 is n/a */
	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_2, 0, 0xffff,
			     PCI_REGION_TYPE, 0);
	ut_assertnonnull(bar);
	ut_asserteq(PCI_EA_BAR2_MAGIC, *(u32 *)bar);

	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_3, 0, 0, PCI_REGION_TYPE, 0);
	ut_assertnull(bar);

	bar = dm_pci_map_bar(swap, PCI_BASE_ADDRESS_4, 0, 0x100000ffff,
			     PCI_REGION_TYPE, 0);
	ut_assertnonnull(bar);
	ut_asserteq(PCI_EA_BAR4_MAGIC, *(u32 *)bar);

	return 0;
}
DM_TEST(dm_test_pci_ea, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test the dev_read_addr_pci() function */
static int dm_test_pci_addr_flat(struct unit_test_state *uts)
{
	struct udevice *swap1f, *swap1;
	ulong io_addr, mem_addr;
	fdt_addr_t size;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &swap1f));
	io_addr = dm_pci_read_bar32(swap1f, 0);
	ut_asserteq(io_addr, dev_read_addr_pci(swap1f, &size));
	ut_asserteq(0, size);

	/*
	 * This device has both I/O and MEM spaces but the MEM space appears
	 * first
	 */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1, 0), &swap1));
	mem_addr = dm_pci_read_bar32(swap1, 1);
	ut_asserteq(mem_addr, dev_read_addr_pci(swap1, &size));
	ut_asserteq(0, size);

	return 0;
}
DM_TEST(dm_test_pci_addr_flat, UTF_SCAN_PDATA | UTF_SCAN_FDT |
		UTF_FLAT_TREE);

/*
 * Test the dev_read_addr_pci() function with livetree. That function is
 * not currently fully implemented, in that it fails to return the BAR address.
 * Once that is implemented this test can be removed and dm_test_pci_addr_flat()
 * can be used for both flattree and livetree by removing the UTF_FLAT_TREE
 * flag above.
 */
static int dm_test_pci_addr_live(struct unit_test_state *uts)
{
	struct udevice *swap1f, *swap1;
	fdt_size_t size;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &swap1f));
	ut_asserteq_64(FDT_ADDR_T_NONE, dev_read_addr_pci(swap1f, &size));
	ut_asserteq(0, size);

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1, 0), &swap1));
	ut_asserteq_64(FDT_ADDR_T_NONE, dev_read_addr_pci(swap1, &size));
	ut_asserteq(0, size);

	return 0;
}
DM_TEST(dm_test_pci_addr_live, UTF_SCAN_PDATA | UTF_SCAN_FDT | UTF_LIVE_TREE);

/* Test device_is_on_pci_bus() */
static int dm_test_pci_on_bus(struct unit_test_state *uts)
{
	struct udevice *dev;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &dev));
	ut_asserteq(true, device_is_on_pci_bus(dev));
	ut_asserteq(false, device_is_on_pci_bus(dev_get_parent(dev)));
	ut_asserteq(true, device_is_on_pci_bus(dev));

	return 0;
}
DM_TEST(dm_test_pci_on_bus, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test support for multiple memory regions enabled via
 * CONFIG_PCI_REGION_MULTI_ENTRY. When this feature is not enabled,
 * only the last region of one type is stored. In this test-case,
 * we have 2 memory regions, the first at 0x3000.0000 and the 2nd
 * at 0x3100.0000. A correct test results now in BAR1 located at
 * 0x3000.0000.
 */
static int dm_test_pci_region_multi(struct unit_test_state *uts)
{
	struct udevice *dev;
	ulong mem_addr;

	/* Test memory BAR1 on bus#1 */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &dev));
	mem_addr = dm_pci_read_bar32(dev, 1);
	ut_asserteq(mem_addr, 0x30000000);

	return 0;
}
DM_TEST(dm_test_pci_region_multi, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test the translation of PCI bus addresses to physical addresses using the
 * ranges from bus#1.
 */
static int dm_test_pci_bus_to_phys(struct unit_test_state *uts)
{
	unsigned long mask = PCI_REGION_TYPE;
	unsigned long flags = PCI_REGION_MEM;
	struct udevice *dev;
	phys_addr_t phys_addr;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &dev));

	/* Before any of the ranges. */
	phys_addr = dm_pci_bus_to_phys(dev, 0x20000000, 0x400, mask, flags);
	ut_asserteq(0, phys_addr);

	/* Identity range: whole, start, mid, end */
	phys_addr = dm_pci_bus_to_phys(dev, 0x2ffff000, 0x2000, mask, flags);
	ut_asserteq(0, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x30000000, 0x2000, mask, flags);
	ut_asserteq(0x30000000, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x30000000, 0x1000, mask, flags);
	ut_asserteq(0x30000000, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x30000abc, 0x12, mask, flags);
	ut_asserteq(0x30000abc, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x30000800, 0x1800, mask, flags);
	ut_asserteq(0x30000800, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x30008000, 0x1801, mask, flags);
	ut_asserteq(0, phys_addr);

	/* Translated range: whole, start, mid, end */
	phys_addr = dm_pci_bus_to_phys(dev, 0x30fff000, 0x2000, mask, flags);
	ut_asserteq(0, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x31000000, 0x2000, mask, flags);
	ut_asserteq(0x3e000000, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x31000000, 0x1000, mask, flags);
	ut_asserteq(0x3e000000, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x31000abc, 0x12, mask, flags);
	ut_asserteq(0x3e000abc, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x31000800, 0x1800, mask, flags);
	ut_asserteq(0x3e000800, phys_addr);
	phys_addr = dm_pci_bus_to_phys(dev, 0x31008000, 0x1801, mask, flags);
	ut_asserteq(0, phys_addr);

	/* Beyond all of the ranges. */
	phys_addr = dm_pci_bus_to_phys(dev, 0x32000000, 0x400, mask, flags);
	ut_asserteq(0, phys_addr);

	return 0;
}
DM_TEST(dm_test_pci_bus_to_phys, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test the translation of physical addresses to PCI bus addresses using the
 * ranges from bus#1.
 */
static int dm_test_pci_phys_to_bus(struct unit_test_state *uts)
{
	unsigned long mask = PCI_REGION_TYPE;
	unsigned long flags = PCI_REGION_MEM;
	struct udevice *dev;
	pci_addr_t pci_addr;

	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(1, 0x08, 0), &dev));

	/* Before any of the ranges. */
	pci_addr = dm_pci_phys_to_bus(dev, 0x20000000, 0x400, mask, flags);
	ut_asserteq(0, pci_addr);

	/* Identity range: partial overlap, whole, start, mid, end */
	pci_addr = dm_pci_phys_to_bus(dev, 0x2ffff000, 0x2000, mask, flags);
	ut_asserteq(0, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x30000000, 0x2000, mask, flags);
	ut_asserteq(0x30000000, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x30000000, 0x1000, mask, flags);
	ut_asserteq(0x30000000, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x30000abc, 0x12, mask, flags);
	ut_asserteq(0x30000abc, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x30000800, 0x1800, mask, flags);
	ut_asserteq(0x30000800, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x30008000, 0x1801, mask, flags);
	ut_asserteq(0, pci_addr);

	/* Translated range: partial overlap, whole, start, mid, end */
	pci_addr = dm_pci_phys_to_bus(dev, 0x3dfff000, 0x2000, mask, flags);
	ut_asserteq(0, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x3e000000, 0x2000, mask, flags);
	ut_asserteq(0x31000000, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x3e000000, 0x1000, mask, flags);
	ut_asserteq(0x31000000, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x3e000abc, 0x12, mask, flags);
	ut_asserteq(0x31000abc, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x3e000800, 0x1800, mask, flags);
	ut_asserteq(0x31000800, pci_addr);
	pci_addr = dm_pci_phys_to_bus(dev, 0x3e008000, 0x1801, mask, flags);
	ut_asserteq(0, pci_addr);

	/* Beyond all of the ranges. */
	pci_addr = dm_pci_phys_to_bus(dev, 0x3f000000, 0x400, mask, flags);
	ut_asserteq(0, pci_addr);

	return 0;
}
DM_TEST(dm_test_pci_phys_to_bus, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test that auto-configuration skips a BAR which has no size. The swap_case
 * emulator has a 64-bit BAR (BAR2/3) which reports its type bits but no size,
 * as a disabled device does. Auto-config must skip it silently, rather than
 * asking for a zero-length region and printing a failure.
 */
static int dm_test_pci_no_size_bar(struct unit_test_state *uts)
{
	struct udevice *bus;

	/* Probing the bus runs auto-config, which must produce no error */
	ut_assertok(uclass_get_device(UCLASS_PCI, 0, &bus));
	ut_assert_console_end();

	return 0;
}
DM_TEST(dm_test_pci_no_size_bar, UTF_SCAN_PDATA | UTF_SCAN_FDT | UTF_CONSOLE);

/*
 * Test that 'pci,no-autoconfig' on a bus disables auto-configuration for every
 * device on it. The device on bus 3 would normally have its BARs assigned, but
 * with the property set they are left unassigned.
 */
static int dm_test_pci_no_autoconfig(struct unit_test_state *uts)
{
	struct udevice *bus, *swap;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 3, &bus));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(3, 0x00, 0), &swap));

	/* Auto-config is disabled, so the BARs are left unassigned */
	ut_asserteq(0, dm_pci_read_bar32(swap, 0));
	ut_asserteq(0, dm_pci_read_bar32(swap, 1));

	return 0;
}
DM_TEST(dm_test_pci_no_autoconfig, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Check a bridge's primary, secondary and subordinate bus numbers */
static int check_bus_regs(struct unit_test_state *uts, struct udevice *bridge,
			  int primary, int secondary, int subordinate)
{
	u8 val;

	ut_assertok(dm_pci_read_config8(bridge, PCI_PRIMARY_BUS, &val));
	ut_asserteq(primary, val);
	ut_assertok(dm_pci_read_config8(bridge, PCI_SECONDARY_BUS, &val));
	ut_asserteq(secondary, val);
	ut_assertok(dm_pci_read_config8(bridge, PCI_SUBORDINATE_BUS, &val));
	ut_asserteq(subordinate, val);

	return 0;
}

/* Test bridges and the devices behind them */
static int dm_test_pci_bridge(struct unit_test_state *uts)
{
	ulong mem_base, mem_limit, io_base, io_limit;
	struct udevice *bus, *bridge, *swap, *dev;
	u16 base16, limit16, vendor, device;
	u8 base8, limit8;
	u32 addr;

	/*
	 * The bridge on bus 2 takes the next number after all the buses,
	 * including the range reserved by pci4 (bus 16), so not 3, which is
	 * pci3. Its registers are relative to the controller
	 */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 2, &bus));
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x20, &bridge));
	ut_asserteq_ptr(bus, dev_get_parent(bridge));
	ut_assertok(check_bus_regs(uts, bridge, 0, 0x1e, 0x1e));

	/*
	 * pci4 has absolute bus numbers, so its bridge is numbered within its
	 * range and its registers hold the same numbers
	 */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x10, &bus));
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x11, &bridge));
	ut_asserteq_ptr(bus, dev_get_parent(bridge));
	ut_assertok(dm_pci_read_config16(bridge, PCI_DEVICE_ID, &device));
	ut_asserteq(SANDBOX_PCI_BRIDGE_EMUL_ID, device);
	ut_assertok(check_bus_regs(uts, bridge, 0x10, 0x11, 0x11));

	/* the device behind the bridge, which is its only child */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0x11, 0, 0), &swap));
	ut_asserteq_ptr(bridge, dev_get_parent(swap));
	ut_assertok(device_find_first_child(bridge, &dev));
	ut_asserteq_ptr(swap, dev);
	ut_assertok(device_find_next_child(&dev));
	ut_assertnull(dev);
	ut_assertok(dm_pci_read_config16(swap, PCI_VENDOR_ID, &vendor));
	ut_asserteq(SANDBOX_PCI_VENDOR_ID, vendor);
	ut_assertok(dm_pci_read_config16(swap, PCI_DEVICE_ID, &device));
	ut_asserteq(SANDBOX_PCI_SWAP_CASE_EMUL_ID, device);

	/* its memory BAR is inside the bridge's memory window */
	ut_assertok(dm_pci_read_config16(bridge, PCI_MEMORY_BASE, &base16));
	ut_assertok(dm_pci_read_config16(bridge, PCI_MEMORY_LIMIT, &limit16));
	mem_base = (ulong)(base16 & PCI_MEMORY_RANGE_MASK) << 16;
	mem_limit = (ulong)(limit16 & PCI_MEMORY_RANGE_MASK) << 16 | 0xfffff;
	ut_asserteq(0xa0000000, mem_base);
	addr = dm_pci_read_bar32(swap, 1);
	ut_assert(addr >= mem_base && addr <= mem_limit);

	/* and its I/O BAR inside the I/O window */
	ut_assertok(dm_pci_read_config8(bridge, PCI_IO_BASE, &base8));
	ut_assertok(dm_pci_read_config8(bridge, PCI_IO_LIMIT, &limit8));
	ut_assertok(dm_pci_read_config16(bridge, PCI_IO_BASE_UPPER16,
					 &base16));
	ut_assertok(dm_pci_read_config16(bridge, PCI_IO_LIMIT_UPPER16,
					 &limit16));
	io_base = (ulong)base16 << 16 | (base8 & PCI_IO_RANGE_MASK) << 8;
	io_limit = (ulong)limit16 << 16 | (limit8 & PCI_IO_RANGE_MASK) << 8 |
		0xfff;
	ut_asserteq(0xa1000000, io_base);
	addr = dm_pci_read_bar32(swap, 0);
	ut_assert(addr >= io_base && addr <= io_limit);

	return 0;
}
DM_TEST(dm_test_pci_bridge, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Read a bridge's memory window */
static int read_mem_window(struct unit_test_state *uts, struct udevice *bridge,
			   ulong *basep, ulong *limitp)
{
	u16 base16, limit16;

	ut_assertok(dm_pci_read_config16(bridge, PCI_MEMORY_BASE, &base16));
	ut_assertok(dm_pci_read_config16(bridge, PCI_MEMORY_LIMIT, &limit16));
	*basep = (ulong)(base16 & PCI_MEMORY_RANGE_MASK) << 16;
	*limitp = (ulong)(limit16 & PCI_MEMORY_RANGE_MASK) << 16 | 0xfffff;

	return 0;
}

/* Read a bridge's I/O window */
static int read_io_window(struct unit_test_state *uts, struct udevice *bridge,
			  ulong *basep, ulong *limitp)
{
	u16 base16, limit16;
	u8 base8, limit8;

	ut_assertok(dm_pci_read_config8(bridge, PCI_IO_BASE, &base8));
	ut_assertok(dm_pci_read_config8(bridge, PCI_IO_LIMIT, &limit8));
	ut_assertok(dm_pci_read_config16(bridge, PCI_IO_BASE_UPPER16,
					 &base16));
	ut_assertok(dm_pci_read_config16(bridge, PCI_IO_LIMIT_UPPER16,
					 &limit16));
	*basep = (ulong)base16 << 16 | (base8 & PCI_IO_RANGE_MASK) << 8;
	*limitp = (ulong)limit16 << 16 | (limit8 & PCI_IO_RANGE_MASK) << 8 |
		0xfff;

	return 0;
}

/*
 * Test that resources are allocated largest first. Bus 2 has a bridge at
 * device 2 with a 2MB BAR behind it and small devices at 1 and 1f. The
 * bridge's window, which must be aligned to 2MB, is placed first, with the
 * small BARs after it; in device order the window would start at 1MB and
 * need 3MB. The I/O window likewise comes first, so that its 4KB alignment
 * costs nothing.
 */
static int dm_test_pci_alloc_order(struct unit_test_state *uts)
{
	struct udevice *bus, *bridge, *swap1, *swap1f, *behind;
	ulong base, limit;

	if (!CONFIG_IS_ENABLED(PCI_PNP_LARGEST_FIRST))
		return -EAGAIN;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 2, &bus));
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x20, &bridge));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(2, 1, 0), &swap1));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(2, 0x1f, 0), &swap1f));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0x20, 0, 0), &behind));

	/* the 2MB window is first, then the small BARs in device order */
	ut_assertok(read_mem_window(uts, bridge, &base, &limit));
	ut_asserteq(0x50000000, base);
	ut_asserteq(0x501fffff, limit);
	ut_asserteq(0x50000000, dm_pci_read_bar32(behind, 1));
	ut_asserteq(0x50200000, dm_pci_read_bar32(swap1, 1));
	ut_asserteq(0x50200100, dm_pci_read_bar32(swap1f, 1));

	/* the same for I/O, where the window is 4KB */
	ut_assertok(read_io_window(uts, bridge, &base, &limit));
	ut_asserteq(0x60000000, base);
	ut_asserteq(0x60000fff, limit);
	ut_asserteq(0x60000000, dm_pci_read_bar32(behind, 0) & ~1);
	ut_asserteq(0x60001000, dm_pci_read_bar32(swap1, 0) & ~1);
	ut_asserteq(0x60001004, dm_pci_read_bar32(swap1f, 0) & ~1);

	return 0;
}
DM_TEST(dm_test_pci_alloc_order, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/*
 * Test a bridge behind a bridge: the outer window must be sized from the bus
 * two levels down and the inner bus must not take its space from the root
 * bus while the root bus is still scanning. Bus 5 has a small device and a
 * bridge to a bridge to a device with a 2MB BAR
 */
static int dm_test_pci_alloc_nested(struct unit_test_state *uts)
{
	struct udevice *bus, *outer, *inner, *swap1, *bottom;
	ulong base, limit;

	if (!CONFIG_IS_ENABLED(PCI_PNP_LARGEST_FIRST))
		return -EAGAIN;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 5, &bus));
	ut_assertok(device_find_first_child_by_uclass(bus, UCLASS_PCI,
						      &outer));
	ut_assertok(device_find_first_child_by_uclass(outer, UCLASS_PCI,
						      &inner));
	ut_assertok(device_find_first_child(inner, &bottom));
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(5, 1, 0), &swap1));

	ut_assertok(read_mem_window(uts, outer, &base, &limit));
	ut_asserteq(0x90000000, base);
	ut_asserteq(0x901fffff, limit);
	ut_assertok(read_mem_window(uts, inner, &base, &limit));
	ut_asserteq(0x90000000, base);
	ut_asserteq(0x901fffff, limit);
	ut_asserteq(0x90000000, dm_pci_read_bar32(bottom, 1));
	ut_asserteq(0x90200000, dm_pci_read_bar32(swap1, 1));

	ut_assertok(read_io_window(uts, outer, &base, &limit));
	ut_asserteq(0x91000000, base);
	ut_asserteq(0x91000fff, limit);
	ut_assertok(read_io_window(uts, inner, &base, &limit));
	ut_asserteq(0x91000000, base);
	ut_asserteq(0x91000fff, limit);
	ut_asserteq(0x91000000, dm_pci_read_bar32(bottom, 0) & ~1);
	ut_asserteq(0x91001000, dm_pci_read_bar32(swap1, 0) & ~1);

	return 0;
}
DM_TEST(dm_test_pci_alloc_nested, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Test the last bus number, including when there are no buses */
static int dm_test_pci_last_busno(struct unit_test_state *uts)
{
	struct udevice *bus;

	/* pci4 is bus 0x10 and reserves its range, up to 0x1f */
	ut_asserteq(0x1f, pci_last_busno());

	ut_assertok(uclass_find_first_device(UCLASS_PCI, &bus));
	while (bus) {
		ut_assertok(device_unbind(bus));
		ut_assertok(uclass_find_first_device(UCLASS_PCI, &bus));
	}
	ut_asserteq(-1, pci_last_busno());

	return 0;
}
DM_TEST(dm_test_pci_last_busno, UTF_SCAN_PDATA | UTF_SCAN_FDT);
/*
 * Test that a root bus with absolute bus numbers reserves its range before
 * it is probed, so the order of probing does not matter
 */
static int dm_test_pci_bus_abs_order(struct unit_test_state *uts)
{
	struct udevice *bus, *bridge;

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x10, &bus));
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x11, &bridge));
	ut_assertok(check_bus_regs(uts, bridge, 0x10, 0x11, 0x11));

	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 2, &bus));
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x20, &bridge));
	ut_asserteq_ptr(bus, dev_get_parent(bridge));
	ut_assertok(check_bus_regs(uts, bridge, 0, 0x1e, 0x1e));

	return 0;
}
DM_TEST(dm_test_pci_bus_abs_order, UTF_SCAN_PDATA | UTF_SCAN_FDT);

/* Probe pci4 with a different bus-range and restore it afterwards */
static int probe_with_range(const fdt32_t *range, int len)
{
	fdt32_t old[2];
	struct udevice *bus;
	const void *prop;
	ofnode node;
	int ret, ret2;

	ret = uclass_find_device_by_seq(UCLASS_PCI, 0x10, &bus);
	if (ret)
		return ret;
	node = dev_ofnode(bus);
	prop = ofnode_read_prop(node, "bus-range", NULL);
	if (!prop)
		return -ENOENT;
	memcpy(old, prop, sizeof(old));
	ret = ofnode_write_prop(node, "bus-range", range, len, true);
	if (ret)
		return ret;
	ret = device_probe(bus);
	ret2 = ofnode_write_prop(node, "bus-range", old, sizeof(old), true);

	return ret2 ? ret2 : ret;
}

/* Test the errors from a root bus with absolute bus numbers */
static int dm_test_pci_bus_abs_errors(struct unit_test_state *uts)
{
	fdt32_t range[2] = { cpu_to_fdt32(0x10), cpu_to_fdt32(0x10) };

	/* no room for the bridge's bus */
	ut_asserteq(-ENOSPC, probe_with_range(range, sizeof(range)));

	/* a range with only one cell, or which ends before it starts */
	ut_asserteq(-EINVAL, probe_with_range(range, sizeof(fdt32_t)));
	range[1] = cpu_to_fdt32(0xf);
	ut_asserteq(-EINVAL, probe_with_range(range, sizeof(range)));

	return 0;
}
DM_TEST(dm_test_pci_bus_abs_errors, UTF_SCAN_PDATA | UTF_SCAN_FDT |
	UTF_LIVE_TREE);

/**
 * probe_fw_bridge() - Probe a bridge as if firmware had set up PCI
 *
 * @uts: Test state
 * @bridge: Bridge to probe, whose root bus must already be probed
 * @sec: Secondary bus number which the firmware gave the bridge
 * Return: 0 if OK, -ve on error
 */
static int probe_fw_bridge(struct unit_test_state *uts, struct udevice *bridge,
			   int sec)
{
	/* start again, as if the bridge were newly bound */
	if (device_active(bridge))
		ut_assertok(device_remove(bridge, DM_REMOVE_NORMAL));
	bridge->seq_ = -1;

	ut_assertok(dm_pci_write_config8(bridge, PCI_SECONDARY_BUS, sec));
	ut_assertok(device_probe(bridge));

	return 0;
}

static int check_fw_numbering(struct unit_test_state *uts)
{
	struct udevice *bus, *bridge2, *bridge4;

	/* bus 2 does not have absolute numbers, so the register is ignored */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 2, &bus));
	ut_assertok(device_find_first_child_by_uclass(bus, UCLASS_PCI,
						      &bridge2));
	ut_assert(!device_active(bridge2));
	ut_assertok(probe_fw_bridge(uts, bridge2, 3));
	ut_asserteq(0x20, dev_seq(bridge2));

	/* pci4's bridge keeps the number the firmware gave it */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0x10, &bus));
	ut_assertok(device_find_first_child_by_uclass(bus, UCLASS_PCI,
						      &bridge4));
	ut_assert(!device_active(bridge4));
	ut_assertok(probe_fw_bridge(uts, bridge4, 0x15));
	ut_asserteq(0x15, dev_seq(bridge4));

	/* unless it is outside the range, or not set up */
	ut_assertok(probe_fw_bridge(uts, bridge4, 0x20));
	ut_asserteq(0x21, dev_seq(bridge4));
	ut_assertok(probe_fw_bridge(uts, bridge4, 0));
	ut_asserteq(0x21, dev_seq(bridge4));

	/* or already in use */
	ut_assertok(device_remove(bridge2, DM_REMOVE_NORMAL));
	bridge2->seq_ = 0x15;
	ut_assertok(probe_fw_bridge(uts, bridge4, 0x15));
	ut_asserteq(0x20, dev_seq(bridge4));

	return 0;
}

/*
 * Test numbering the buses behind bridges which firmware has set up, as when
 * U-Boot runs as a coreboot payload and does not configure PCI itself
 */
static int dm_test_pci_bus_fw(struct unit_test_state *uts)
{
	ulong flags = gd->flags;
	int ret;

	gd->flags |= GD_FLG_SKIP_LL_INIT;
	ret = check_fw_numbering(uts);
	gd->flags = flags;

	return ret;
}
DM_TEST(dm_test_pci_bus_fw, UTF_SCAN_PDATA | UTF_SCAN_FDT);
