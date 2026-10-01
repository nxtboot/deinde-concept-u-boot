// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Access to the System Management Network (SMN) on AMD EPYC Turin, for
 * poking at the NBIO/IOHC, data-fabric and SMU registers when bringing the
 * board up. SMN is reached through an index/data pair in a root complex's
 * config space; any root bus's pair reaches the same space.
 */

#include <command.h>
#include <pci.h>
#include <string.h>
#include <vsprintf.h>
#include <asm/pci.h>

#define SMN_INDEX	0xb8
#define SMN_DATA	0xbc

static ulong smn_read(int bus, u32 addr)
{
	ulong val;

	pci_x86_write_config(PCI_BDF(bus, 0, 0), SMN_INDEX, addr, PCI_SIZE_32);
	pci_x86_read_config(PCI_BDF(bus, 0, 0), SMN_DATA, &val, PCI_SIZE_32);

	return val;
}

static void smn_write(int bus, u32 addr, u32 val)
{
	pci_x86_write_config(PCI_BDF(bus, 0, 0), SMN_INDEX, addr, PCI_SIZE_32);
	pci_x86_write_config(PCI_BDF(bus, 0, 0), SMN_DATA, val, PCI_SIZE_32);
}

static int do_smn(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[])
{
	int bus = 0;
	u32 addr;

	if (argc < 3)
		return CMD_RET_USAGE;
	addr = hextoul(argv[2], NULL);
	if (!strcmp(argv[1], "md")) {
		int count = argc > 3 ? hextoul(argv[3], NULL) : 1;
		int i;

		if (argc > 4)
			bus = hextoul(argv[4], NULL);
		for (i = 0; i < count; i++) {
			if (!(i & 3))
				printf("%s%08x:", i ? "\n" : "", addr + i * 4);
			printf(" %08lx", smn_read(bus, addr + i * 4));
		}
		printf("\n");
	} else if (!strcmp(argv[1], "mw")) {
		if (argc < 4)
			return CMD_RET_USAGE;
		if (argc > 4)
			bus = hextoul(argv[4], NULL);
		smn_write(bus, addr, hextoul(argv[3], NULL));
	} else {
		return CMD_RET_USAGE;
	}

	return 0;
}

U_BOOT_LONGHELP(smn,
	"md <addr> [<count>] [<bus>] - read <count> words from SMN address\n"
	"smn mw <addr> <value> [<bus>]  - write a word to SMN address\n"
	"<bus> is the root bus whose index/data pair to use (default 0)");

U_BOOT_CMD(smn, 5, 1, do_smn, "AMD System Management Network access",
	   smn_help_text);
