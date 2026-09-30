// SPDX-License-Identifier: GPL-2.0+
/*
 * Show the FCH's interrupt routing and power-management registers
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <display_options.h>
#include <mapmem.h>
#include <asm/io.h>

/* Interrupt routing, indexed by source, with bit 7 selecting I/O APIC mode */
#define PCI_INTR_INDEX		0xc00
#define PCI_INTR_DATA		0xc01
#define PCI_INTR_APIC		BIT(7)
#define PCI_INTR_ROUTES		0x80
#define PCI_INTR_NONE		0x1f

#define ACPIMMIO_PMIO		0xfed80300
#define PMIO_SIZE		0x100

static u8 intr_read(uint index)
{
	outb(index, PCI_INTR_INDEX);

	return inb(PCI_INTR_DATA);
}

static void fch_show_irq(bool all)
{
	int i;

	printf("Index  PIC  APIC\n");
	for (i = 0; i < PCI_INTR_ROUTES; i++) {
		u8 pic = intr_read(i), apic = intr_read(i | PCI_INTR_APIC);

		/* an unrouted source reads as 0x1f, or 0 if never set */
		if (!all && (pic == PCI_INTR_NONE || !pic) &&
		    (apic == PCI_INTR_NONE || !apic))
			continue;
		printf("   %02x   %02x    %02x\n", i, pic, apic);
	}
}

static int do_fch(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	if (argc < 2)
		return CMD_RET_USAGE;
	if (!strcmp(argv[1], "irq")) {
		fch_show_irq(argc > 2 && !strcmp(argv[2], "-a"));
	} else if (!strcmp(argv[1], "pm")) {
		void *pmio = map_sysmem(ACPIMMIO_PMIO, PMIO_SIZE);

		print_buffer(ACPIMMIO_PMIO, pmio, 4, PMIO_SIZE / 4, 0);
		unmap_sysmem(pmio);
	} else {
		return CMD_RET_USAGE;
	}

	return 0;
}

U_BOOT_LONGHELP(fch,
	"irq [-a]  - show the interrupt routing (PIC and I/O APIC mode)\n"
	"    -a  show unrouted sources too\n"
	"fch pm        - dump the power-management registers");

U_BOOT_CMD(fch, 3, 1, do_fch, "AMD FCH (southbridge) information",
	   fch_help_text);
