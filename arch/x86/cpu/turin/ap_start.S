/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Real-mode start-up routine for the application processors of AMD Turin
 *
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * When the SMU releases a thread it starts in real mode at its reset vector,
 * 16 bytes below the end of a 64KB segment whose base is in %cs. This routine
 * is copied into that segment at TURIN_AP_CODE_OFFSET, with a jump to it at
 * the reset vector. It loads a list of MSRs prepared by the boot processor,
 * starting with the memory map and MTRRs, since until those are set the AP
 * cannot reach DRAM below 1MB; then the microcode patch. It records its APIC
 * ID and halts. Everything is addressed through %cs, whose base is not a
 * real-mode segment, so %ds is never used
 */

#include <asm/msr-index.h>
#include <asm/processor-flags.h>
#include <asm/arch/ap.h>

#define OFF(sym)	(TURIN_AP_CODE_OFFSET + (sym) - turin_ap_start)

	.section .text.turin_ap_start
	.code16
	.globl	turin_ap_start
turin_ap_start:
	cli

	/* one thread at a time, since the two threads of a core share a patch */
1:	lock btsl $0, %cs:OFF(ap_lock)
	jc	1b

	/* each entry is the MSR index, then the low and high words */
	movw	$OFF(ap_msrs), %si
	movl	%cs:OFF(ap_msr_count), %ebx
2:	testl	%ebx, %ebx
	jz	3f
	movl	%cs:(%si), %ecx
	movl	%cs:4(%si), %eax
	movl	%cs:8(%si), %edx
	wrmsr
	addw	$12, %si
	decl	%ebx
	jmp	2b

	/*
	 * load the patch on every thread, as openSIL does, even when the
	 * core's other thread has loaded it: the level reads the same but
	 * each thread needs its own load
	 */
3:	movl	%cs:OFF(ap_ucode_lo), %eax
	orl	%cs:OFF(ap_ucode_hi), %eax
	jz	4f
	movl	%cs:OFF(ap_ucode_lo), %eax
	movl	%cs:OFF(ap_ucode_hi), %edx
	movl	$MSR_AMD64_PATCH_LOADER, %ecx
	wrmsr

	/* every thread enables its own local APIC */
4:	movl	%cs:OFF(ap_apic_lo), %eax
	movl	%cs:OFF(ap_apic_hi), %edx
	movl	$MSR_IA32_APICBASE, %ecx
	wrmsr

	/* the initial APIC ID is in bits 31:24 of %ebx */
	movl	$1, %eax
	cpuid
	shrl	$24, %ebx
	movw	%cs:OFF(ap_count), %di
	movb	%bl, %cs:OFF(ap_ids)(%di)

	/*
	 * The last thread of each core sets CombineCr0Cd in TW_CFG, as
	 * openSIL does: the core then combines CR0.CD across its threads, so
	 * one thread disabling its cache (as the OS does while setting MTRRs)
	 * cannot leave the two threads with different views of memory. It
	 * must not be set before the last thread runs, since a thread still
	 * held in reset has CR0.CD set. Bits 15:8 of CPUID 8000001e EBX give
	 * the threads per core, minus one
	 */
	movl	$0x8000001e, %eax
	cpuid
	shrl	$8, %ebx
	andl	$0xff, %ebx
	movb	%cs:OFF(ap_ids)(%di), %al
	andb	%bl, %al
	cmpb	%bl, %al
	jne	6f
	movl	$MSR_AMD64_TW_CFG, %ecx
	rdmsr
	orl	$TW_CFG_COMBINE_CR0_CD_HI, %edx
	wrmsr
6:
	/* enable this thread's caches, as the OS expects */
	movl	%cr0, %eax
	andl	$~(X86_CR0_CD | X86_CR0_NW), %eax
	movl	%eax, %cr0

	incl	%cs:OFF(ap_count)
	movl	$0, %cs:OFF(ap_lock)

5:	cli
	hlt
	jmp	5b

	/* This matches struct turin_ap_params */
	.align	4
	.globl	turin_ap_params
turin_ap_params:
ap_lock:
	.long	0
ap_ucode_lo:
	.long	0
ap_ucode_hi:
	.long	0
ap_apic_lo:
	.long	0
ap_apic_hi:
	.long	0
ap_msr_count:
	.long	0
ap_count:
	.long	0
ap_ids:
	.fill	TURIN_AP_MAX_IDS, 1, 0
ap_msrs:
	.fill	TURIN_AP_MAX_MSRS * 12, 1, 0
	.globl	turin_ap_end
turin_ap_end:
