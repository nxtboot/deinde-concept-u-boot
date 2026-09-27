// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI emulation device for a PCI-to-PCI bridge, for sandbox tests
 *
 * The bridge has a type-1 header with no BARs and no capabilities. Its
 * bus-number, window and control registers read back what was written,
 * so tests can check how U-Boot programmed them
 *
 * Copyright 2026 Simon Glass
 */

#define LOG_CATEGORY UCLASS_PCI_EMUL

#include <dm.h>
#include <pci.h>
#include <asm/test.h>
#include <linux/bitops.h>

/**
 * struct pci_bridge_emul_priv - private data for this device
 *
 * @cfg: Configuration space, as the bridge holds it
 */
struct pci_bridge_emul_priv {
	u8 cfg[PCI_CFG_SPACE_SIZE];
};

/*
 * is_read_only() - Check whether a configuration-space byte is read-only
 *
 * The IDs, class, header type and (empty) capability list are fixed. The
 * BARs and expansion ROM are not implemented, so read as zero, which tells
 * auto-configuration that the bridge has none
 */
static bool is_read_only(uint offset)
{
	return offset < PCI_COMMAND ||
		(offset >= PCI_CLASS_REVISION && offset < PCI_CACHE_LINE_SIZE) ||
		offset == PCI_HEADER_TYPE || offset == PCI_CAPABILITY_LIST ||
		(offset >= PCI_BASE_ADDRESS_0 && offset < PCI_PRIMARY_BUS) ||
		(offset >= PCI_ROM_ADDRESS1 && offset < PCI_INTERRUPT_LINE);
}

static int pci_bridge_emul_read_config(const struct udevice *emul,
				       uint offset, ulong *valuep,
				       enum pci_size_t size)
{
	struct pci_bridge_emul_priv *priv = dev_get_priv(emul);
	int len = 1 << size;	/* 1, 2 or 4 bytes */
	ulong val = 0;
	int i;

	if (offset + len > PCI_CFG_SPACE_SIZE) {
		*valuep = pci_get_ff(size);
		return 0;
	}
	for (i = len - 1; i >= 0; i--)
		val = val << 8 | priv->cfg[offset + i];
	*valuep = val;

	return 0;
}

static int pci_bridge_emul_write_config(struct udevice *emul, uint offset,
					ulong value, enum pci_size_t size)
{
	struct pci_bridge_emul_priv *priv = dev_get_priv(emul);
	int len = 1 << size;	/* 1, 2 or 4 bytes */
	int i;

	for (i = 0; i < len && offset + i < PCI_CFG_SPACE_SIZE; i++) {
		if (!is_read_only(offset + i))
			priv->cfg[offset + i] = value;
		value >>= 8;
	}

	return 0;
}

static int pci_bridge_emul_probe(struct udevice *dev)
{
	struct pci_bridge_emul_priv *priv = dev_get_priv(dev);
	u8 *cfg = priv->cfg;

	cfg[PCI_VENDOR_ID] = SANDBOX_PCI_VENDOR_ID & 0xff;
	cfg[PCI_VENDOR_ID + 1] = SANDBOX_PCI_VENDOR_ID >> 8;
	cfg[PCI_DEVICE_ID] = SANDBOX_PCI_BRIDGE_EMUL_ID & 0xff;
	cfg[PCI_DEVICE_ID + 1] = SANDBOX_PCI_BRIDGE_EMUL_ID >> 8;
	cfg[PCI_CLASS_DEVICE] = PCI_CLASS_BRIDGE_PCI & 0xff;
	cfg[PCI_CLASS_CODE] = PCI_CLASS_BRIDGE_PCI >> 8;
	cfg[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_BRIDGE;

	/* 32-bit I/O and 64-bit prefetchable windows */
	cfg[PCI_IO_BASE] = PCI_IO_RANGE_TYPE_32;
	cfg[PCI_IO_LIMIT] = PCI_IO_RANGE_TYPE_32;
	cfg[PCI_PREF_MEMORY_BASE] = PCI_PREF_RANGE_TYPE_64;
	cfg[PCI_PREF_MEMORY_LIMIT] = PCI_PREF_RANGE_TYPE_64;

	return 0;
}

static struct dm_pci_emul_ops pci_bridge_emul_ops = {
	.read_config = pci_bridge_emul_read_config,
	.write_config = pci_bridge_emul_write_config,
};

static const struct udevice_id pci_bridge_emul_ids[] = {
	{ .compatible = "sandbox,pci-bridge-emul" },
	{ }
};

U_BOOT_DRIVER(sandbox_pci_bridge_emul) = {
	.name		= "sandbox_pci_bridge_emul",
	.id		= UCLASS_PCI_EMUL,
	.of_match	= pci_bridge_emul_ids,
	.ops		= &pci_bridge_emul_ops,
	.probe		= pci_bridge_emul_probe,
	.priv_auto	= sizeof(struct pci_bridge_emul_priv),
};
