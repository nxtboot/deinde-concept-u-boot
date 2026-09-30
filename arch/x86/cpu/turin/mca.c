// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Show the machine-check banks (Scalable MCA) on AMD EPYC Turin. The status
 * registers survive a warm reset, so this shows what went wrong just before
 * an unexpected reboot.
 */

#include <command.h>
#include <string.h>
#include <vsprintf.h>
#include <asm/msr.h>

#define MSR_MCG_CAP		0x179
#define MSR_SMCA_BASE		0xc0002000
#define SMCA_BANK_STRIDE	0x10
#define SMCA_CTL		0
#define SMCA_STATUS		1
#define SMCA_ADDR		2
#define SMCA_MISC		3
#define SMCA_CONFIG		4
#define SMCA_IPID		5
#define SMCA_SYND		6
#define MCA_STATUS_VAL		BIT_ULL(63)

static u64 mca_msr(int bank, int reg)
{
	u64 val;

	rdmsrl(MSR_SMCA_BASE + bank * SMCA_BANK_STRIDE + reg, val);

	return val;
}

static int do_mca(struct cmd_tbl *cmdtp, int flag, int argc,
		  char *const argv[])
{
	bool all = argc > 1 && !strcmp(argv[1], "all");
	bool clear = argc > 1 && !strcmp(argv[1], "clear");
	int count, bank;
	u64 cap;

	rdmsrl(MSR_MCG_CAP, cap);
	count = cap & 0xff;
	printf("%d banks\n", count);
	for (bank = 0; bank < count; bank++) {
		u64 status = mca_msr(bank, SMCA_STATUS);

		if (!(status & MCA_STATUS_VAL) && !all)
			continue;
		printf("bank %2d: ipid %016llx status %016llx addr %016llx\n",
		       bank, mca_msr(bank, SMCA_IPID), status,
		       mca_msr(bank, SMCA_ADDR));
		printf("         misc %016llx synd %016llx config %016llx\n",
		       mca_msr(bank, SMCA_MISC), mca_msr(bank, SMCA_SYND),
		       mca_msr(bank, SMCA_CONFIG));
		if (clear)
			wrmsrl(MSR_SMCA_BASE + bank * SMCA_BANK_STRIDE +
			       SMCA_STATUS, 0);
	}

	return 0;
}

U_BOOT_LONGHELP(mca,
	"[all|clear] - show banks with a valid error (all: every bank; clear: clear them)");

U_BOOT_CMD(mca, 2, 1, do_mca, "show machine-check banks", mca_help_text);
