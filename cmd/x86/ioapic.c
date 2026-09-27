// SPDX-License-Identifier: GPL-2.0+
/*
 * Show an I/O APIC's identity and redirection table
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <mapmem.h>
#include <vsprintf.h>
#include <asm/io.h>
#include <asm/ioapic.h>

#define IOAPIC_REDTBL		0x10
#define IOAPIC_VER_ENTRIES_SHIFT 16
#define IOAPIC_VER_ENTRIES_MASK	0xff
#define IOAPIC_ENTRY_MASKED	BIT(16)

static u32 ioapic_rd(void *base, u32 reg)
{
	writel(reg, base);

	return readl(base + 0x10);
}

static int do_ioapic(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	ulong addr = IO_APIC_ADDR;
	bool all = false;
	int entries, i;
	void *base;
	u32 ver;

	if (argc > 1 && !strcmp(argv[1], "-a")) {
		all = true;
		argc--;
		argv++;
	}
	if (argc > 1)
		addr = hextoul(argv[1], NULL);
	base = map_sysmem(addr, 0x20);
	ver = ioapic_rd(base, IO_APIC_VER);
	entries = ((ver >> IOAPIC_VER_ENTRIES_SHIFT) & IOAPIC_VER_ENTRIES_MASK) +
		1;
	printf("I/O APIC at %lx: ID %x, version %02x, %d entries\n", addr,
	       ioapic_rd(base, IO_APIC_ID) >> 24, ver & 0xff, entries);
	printf("Pin  Vector  Dest  Delivery  Polarity  Trigger  Mask\n");
	for (i = 0; i < entries; i++) {
		u32 lo = ioapic_rd(base, IOAPIC_REDTBL + 2 * i);
		u32 hi = ioapic_rd(base, IOAPIC_REDTBL + 2 * i + 1);

		if (!all && (lo & IOAPIC_ENTRY_MASKED))
			continue;
		printf("%3d  %02x      %02x    %s  %s  %s  %s\n", i, lo & 0xff,
		       hi >> 24, (lo >> 8) & 7 ? "lowest/other" : "fixed       ",
		       lo & BIT(13) ? "low     " : "high    ",
		       lo & BIT(15) ? "level  " : "edge   ",
		       lo & IOAPIC_ENTRY_MASKED ? "masked" : "");
	}
	unmap_sysmem(base);

	return 0;
}

U_BOOT_LONGHELP(ioapic,
	"[-a] [addr]  - show the I/O APIC at addr (default fec00000)\n"
	"    -a  show masked entries too");

U_BOOT_CMD(ioapic, 3, 1, do_ioapic, "Show an I/O APIC's redirection table",
	   ioapic_help_text);
