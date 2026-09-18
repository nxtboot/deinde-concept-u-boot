// SPDX-License-Identifier: GPL-2.0+
/*
 * Test device path functions
 *
 * Copyright (c) 2020 Heinrich Schuchardt <xypron.glpk@gmx.de>
 */

#include <dm.h>
#include <efi_device_path.h>
#include <efi_loader.h>
#include <pci.h>
#include <dm/device-internal.h>
#include <test/lib.h>
#include <test/test.h>
#include <test/ut.h>

static int lib_test_efi_dp_check_length(struct unit_test_state *uts)
{
	/* end of device path */
	u8 d1[] __aligned(2) = {
		0x7f, 0xff, 0x04, 0x00 };
	/* device path node with length less then 4 */
	u8 d2[] __aligned(2) = {
		0x01, 0x02, 0x02, 0x00, 0x04, 0x00, 0x7f, 0xff, 0x04, 0x00 };
	/* well formed device path */
	u8 d3[] __aligned(2) = {
		0x03, 0x02, 0x08, 0x00, 0x01, 0x00, 0x01, 0x00,
		0x7f, 0xff, 0x04, 0x00 };

	struct efi_device_path *p1 = (struct efi_device_path *)d1;
	struct efi_device_path *p2 = (struct efi_device_path *)d2;
	struct efi_device_path *p3 = (struct efi_device_path *)d3;

	ut_asserteq((ssize_t)-EINVAL, efi_dp_check_length(p1, SIZE_MAX));
	ut_asserteq((ssize_t)sizeof(d1), efi_dp_check_length(p1, sizeof(d1)));
	ut_asserteq((ssize_t)sizeof(d1),
		    efi_dp_check_length(p1, sizeof(d1) + 4));
	ut_asserteq((ssize_t)-1, efi_dp_check_length(p1, sizeof(d1) - 1));

	ut_asserteq((ssize_t)-1, efi_dp_check_length(p2, sizeof(d2)));

	ut_asserteq((ssize_t)-1, efi_dp_check_length(p3, sizeof(d3) - 1));
	ut_asserteq((ssize_t)sizeof(d3), efi_dp_check_length(p3, sizeof(d3)));
	ut_asserteq((ssize_t)sizeof(d3), efi_dp_check_length(p3, SSIZE_MAX));
	ut_asserteq((ssize_t)-EINVAL,
		    efi_dp_check_length(p3, (size_t)SSIZE_MAX + 1));
	ut_asserteq((ssize_t)sizeof(d3),
		    efi_dp_check_length(p3, sizeof(d3) + 4));

	return 0;
}
LIB_TEST(lib_test_efi_dp_check_length, 0);

/**
 * check_dp_text() - Check the text form of a device's device path
 *
 * @uts: Test state
 * @dev: Device to build a path for
 * @expect: Expected text form
 * Return: 0 if OK, 1 on failure
 */
static int check_dp_text(struct unit_test_state *uts, struct udevice *dev,
			 const char *expect)
{
	struct efi_device_path *dp;
	u16 *str;

	dp = efi_dp_from_dev(dev);
	ut_assertnonnull(dp);
	str = efi_dp_str(dp);
	ut_assertnonnull(str);
	printf("%ls\n", str);
	ut_assert_nextline("%s", expect);
	efi_free_pool(str);
	efi_free_pool(dp);

	return 0;
}

/**
 * hex_le32() - Write a 32-bit value as the hex bytes of its little-endian form
 *
 * This is how the vendor-data bytes of a vendor node appear in text
 *
 * @buf: Buffer of at least 9 bytes to write to
 * @val: Value to write
 * Return: @buf
 */
static const char *hex_le32(char *buf, u32 val)
{
	sprintf(buf, "%02x%02x%02x%02x", val & 0xff, (val >> 8) & 0xff,
		(val >> 16) & 0xff, val >> 24);

	return buf;
}

/* Vendor node for the root device, which starts every device path */
#define ROOT_NODE "/VenHw(e61d73b9-a384-4acc-aeab-82e828f3628b,0000000000000000)"

/* Test device paths for devices on a PCI bus */
static int lib_test_efi_dp_pci(struct unit_test_state *uts)
{
	char expect[128], uclass[9], seq[9];
	struct udevice *bus, *dev;

	/* This needs sandbox's PCI buses and virtio device */
	if (!IS_ENABLED(CONFIG_SANDBOX))
		return -EAGAIN;

	/* The root device is a vendor node with U-Boot's GUID */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 0, &bus));
	ut_assertok(check_dp_text(uts, bus, ROOT_NODE "/PciRoot(0x0)"));

	/* A device on the bus gets a PCI node rather than a vendor one */
	ut_assertok(dm_pci_bus_find_bdf(PCI_BDF(0, 0x1f, 0), &dev));
	ut_assertok(check_dp_text(uts, dev,
				  ROOT_NODE "/PciRoot(0x0)/Pci(0x1f,0x0)"));

	/* A second root bus has its own ACPI node */
	ut_assertok(uclass_get_device_by_seq(UCLASS_PCI, 1, &bus));
	ut_assertok(check_dp_text(uts, bus, ROOT_NODE "/PciRoot(0x1)"));

	/* A device which is not on PCI still gets a vendor node */
	ut_assertok(uclass_get_device_by_name(UCLASS_VIRTIO, "sandbox-virtio-blk",
					      &dev));
	snprintf(expect, sizeof(expect), ROOT_NODE
		 "/VenHw(e61d73b9-a384-4acc-aeab-82e828f3628b,%s%s)",
		 hex_le32(uclass, UCLASS_VIRTIO), hex_le32(seq, dev_seq(dev)));
	ut_assertok(check_dp_text(uts, dev, expect));

	return 0;
}
LIB_TEST(lib_test_efi_dp_pci, UTF_DM | UTF_SCAN_FDT | UTF_CONSOLE);
