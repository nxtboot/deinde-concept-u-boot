// SPDX-License-Identifier: GPL-2.0+
/*
 * Write an ACPI Service Processor Management Interface (SPMI) table
 *
 * This tells the OS about an IPMI interface, typically to a BMC, which the
 * devicetree describes with a node compatible with ipmi-kcs, ipmi-smic or
 * ipmi-bt, as in the Linux binding.
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY LOGC_ACPI

#include <log.h>
#include <tables_csum.h>
#include <acpi/acpi_table.h>
#include <dm/acpi.h>
#include <dm/ofnode.h>
#include <linux/string.h>

static const struct {
	const char *compat;
	u8 type;
} ipmi_types[] = {
	{ "ipmi-kcs", ACPI_SPMI_KCS },
	{ "ipmi-smic", ACPI_SPMI_SMIC },
	{ "ipmi-bt", ACPI_SPMI_BT },
};

int acpi_write_spmi(struct acpi_ctx *ctx, const struct acpi_writer *entry)
{
	struct acpi_table_header *header;
	struct acpi_spmi *spmi;
	ofnode node;
	fdt_addr_t addr;
	int i;

	for (i = 0; i < ARRAY_SIZE(ipmi_types); i++) {
		node = ofnode_by_compatible(ofnode_null(), ipmi_types[i].compat);
		if (ofnode_valid(node))
			break;
	}
	if (i == ARRAY_SIZE(ipmi_types))
		return log_msg_ret("node", -ENOENT);
	addr = ofnode_get_addr(node);
	if (addr == FDT_ADDR_T_NONE)
		return log_msg_ret("addr", -EINVAL);

	spmi = ctx->current;
	header = &spmi->header;
	memset(spmi, '\0', sizeof(*spmi));
	acpi_fill_header(header, "SPMI");
	header->length = sizeof(*spmi);
	header->revision = acpi_get_table_revision(ACPITAB_SPMI);

	spmi->interface_type = ipmi_types[i].type;
	spmi->reserved = 1;
	spmi->spec_rev = cpu_to_le16(ACPI_SPMI_IPMI_2_0);

	/*
	 * On x86 the interface is on the LPC bus, in I/O space; elsewhere it
	 * is memory-mapped. The register spacing is the width of each access
	 */
	spmi->base.space_id = IS_ENABLED(CONFIG_X86) ? ACPI_ADDRESS_SPACE_IO :
		ACPI_ADDRESS_SPACE_MEMORY;
	spmi->base.bit_width = 8 * ofnode_read_u32_default(node, "reg-spacing",
							  1);
	spmi->base.access_size = ACPI_ACCESS_SIZE_BYTE_ACCESS;
	spmi->base.addrl = lower_32_bits(addr);
	spmi->base.addrh = upper_32_bits(addr);
	log_debug("IPMI type %d at %llx\n", spmi->interface_type,
		  (unsigned long long)addr);

	acpi_update_checksum(header);
	acpi_add_table(ctx, spmi);
	acpi_inc(ctx, header->length);

	return 0;
}
ACPI_WRITER(5spmi, "SPMI", acpi_write_spmi, 0);
