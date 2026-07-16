// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Minimal SMM setup: relocate each CPU's SMBASE out of the default
 * 0x30000 (which overlaps the MP startup area) into a dedicated RAM
 * buffer, with a resident handler which does nothing but acknowledge
 * the SMI. coreboot has full SMM handlers in place before FSP-S runs;
 * this provides just enough for an SMI to be survivable, to test
 * whether the pcode's BIOS_RESET_CPL handshake needs one serviced
 */

#define LOG_CATEGORY	LOGC_ARCH

#include <log.h>
#include <malloc.h>
#include <asm/io.h>
#include <asm/lapic.h>
#include <asm/mp.h>
#include <asm/msr.h>
#include <asm/arch/cpu.h>
#include <linux/delay.h>
#include <linux/string.h>

/* IA32_APIC_BASE, bit 10 selects x2APIC (MSR-based) register access */
#define MSR_APIC_BASE		0x1b
#define  APIC_BASE_X2APIC	BIT(10)

/* The x2APIC interrupt-command register and its local-APIC-ID register */
#define MSR_X2APIC_ICR		0x830
#define MSR_X2APIC_APICID	0x802

/* Each CPU gets this much SMRAM: handler at +0x8000, state at +0xfc00 */
#define SMM_STRIDE	0x10000

/* The default SMBASE, whose handler entry is at +0x8000 */
#define SMM_DEFAULT	0x30000

/* Communication with the relocation stub (see smm_asm.S) */
#define SMM_MAILBOX	0x37f00
#define SMM_DONE	0x37f04

/* The PCH's SMI enable register; GBL_SMI_EN and EOS gate delivery */
#define SMI_EN		0x1830
#define  SMI_EN_GBL	BIT(0)
#define  SMI_EN_EOS	BIT(1)

/* The stubs, assembled as 16-bit code in smm_asm.S */
extern const char adl_smm_reloc_stub[], adl_smm_reloc_stub_end[];
extern const char adl_smm_perm_stub[], adl_smm_perm_stub_end[];

/*
 * Runs on the CPU being relocated: raise an SMI against ourselves via
 * the local APIC and wait for the relocation handler to acknowledge.
 * The new SMBASE is already in the mailbox. The SDM only permits the
 * 'self' destination shorthand with the fixed delivery mode, so the
 * SMI must be sent by physical destination with our own APIC ID
 */
static void send_self_smi(void)
{
	if (msr_read(MSR_APIC_BASE).lo & APIC_BASE_X2APIC) {
		msr_t icr;

		icr.hi = msr_read(MSR_X2APIC_APICID).lo;
		icr.lo = LAPIC_INT_ASSERT | LAPIC_DM_SMI;
		msr_write(MSR_X2APIC_ICR, icr);
	} else {
		lapic_write(LAPIC_ICR2, SET_LAPIC_DEST_FIELD(lapicid()));
		lapic_write(LAPIC_ICR, LAPIC_INT_ASSERT | LAPIC_DM_SMI);
	}
}

/* SMI counts around the last relocation attempt, for diagnostics */
static volatile u32 smi_count_before, smi_count_after;

static void relocate_cb(void *arg)
{
	int i;

	smi_count_before = msr_read(MSR_SMI_COUNT).lo;
	send_self_smi();
	for (i = 0; i < 1000; i++) {
		if (readl((void *)SMM_DONE))
			break;
		udelay(10);
	}
	smi_count_after = msr_read(MSR_SMI_COUNT).lo;
}

int adl_smm_relocate(int num_cpus)
{
	void *buf;
	int seq;

	log_debug("APIC base %x (x2apic %d)\n", msr_read(MSR_APIC_BASE).lo,
		  !!(msr_read(MSR_APIC_BASE).lo & APIC_BASE_X2APIC));

	buf = memalign(SMM_STRIDE, num_cpus * SMM_STRIDE);
	if (!buf)
		return log_msg_ret("buf", -ENOMEM);
	memset(buf, '\0', num_cpus * SMM_STRIDE);

	/* Install the resident handler at each CPU's new entry point */
	for (seq = 0; seq < num_cpus; seq++)
		memcpy(buf + seq * SMM_STRIDE + 0x8000, adl_smm_perm_stub,
		       adl_smm_perm_stub_end - adl_smm_perm_stub);

	/* Install the relocation handler at the default entry point */
	memcpy((void *)(SMM_DEFAULT + 0x8000), adl_smm_reloc_stub,
	       adl_smm_reloc_stub_end - adl_smm_reloc_stub);

	/* Move each CPU in turn: they share the default save-state area */
	for (seq = 0; seq < num_cpus; seq++) {
		ulong smbase = (ulong)buf + seq * SMM_STRIDE;
		int ret;

		writel(smbase, (void *)SMM_MAILBOX);
		writel(0, (void *)SMM_DONE);
		ret = mp_run_on_cpus(seq, relocate_cb, NULL);
		if (ret)
			return log_msg_ret("run", ret);
		if (!readl((void *)SMM_DONE)) {
			log_warning("CPU %d: SMI not serviced (count %u -> %u)\n",
				    seq, smi_count_before, smi_count_after);
			return log_msg_ret("smi", -ETIMEDOUT);
		}
		log_debug("CPU %d: SMBASE %lx (SMI count %u -> %u)\n", seq,
			  smbase, smi_count_before, smi_count_after);
	}

	/* Let SMIs through at the PCH and arm the edge detector */
	setio_32(SMI_EN, SMI_EN_GBL | SMI_EN_EOS);
	log_debug("SMM ready, SMI_EN %x\n", inl(SMI_EN));

	return 0;
}
