/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * SMM stubs, entered in SMM's big-real mode (16-bit code, but the
 * segments have 4GB limits and their bases are zero, apart from CS,
 * so 32-bit addressing reaches the whole of low memory). These are
 * copied into place as bytes; they use only absolute addresses
 */

/* The mailbox holding the new SMBASE, just below the default handler */
#define SMM_MAILBOX	0x37f00

/* Completion flag, set by the relocation handler before RSM */
#define SMM_DONE	0x37f04

/* The SMBASE field of the save-state area at the default SMBASE */
#define SMM_SAVE_SMBASE	0x3fef8

/* The PCH's SMI enable register in ACPI I/O space; bit 1 is EOS */
#define SMI_EN_PORT	0x1830

.code16
.section .text.adl_smm_stubs, "ax"

/*
 * Relocation handler, copied to 0x38000 (default SMBASE + 0x8000).
 * Each CPU enters here once, from a self-SMI: take the new SMBASE
 * from the mailbox, store it in the save-state area so that RSM
 * makes it live, flag completion and leave. Emits postcode 0xe4
 */
.globl adl_smm_reloc_stub
adl_smm_reloc_stub:
	movb	$0xe4, %al
	outb	%al, $0x80
	movl	$SMM_MAILBOX, %ebx
	movl	(%ebx), %eax
	movl	$SMM_SAVE_SMBASE, %edi
	movl	%eax, (%edi)
	movl	$SMM_DONE, %ebx
	movl	$1, (%ebx)
	rsm
.globl adl_smm_reloc_stub_end
adl_smm_reloc_stub_end:

/*
 * Resident handler, copied to each CPU's relocated SMBASE + 0x8000.
 * Any SMI arriving after relocation lands here: emit postcode 0xe5
 * so the EC's port-80 log shows that an SMI happened, re-arm the
 * PCH's SMI logic via EOS and resume
 */
.globl adl_smm_perm_stub
adl_smm_perm_stub:
	movb	$0xe5, %al
	outb	%al, $0x80
	movw	$SMI_EN_PORT, %dx
	inl	%dx, %eax
	orl	$2, %eax
	outl	%eax, %dx
	rsm
.globl adl_smm_perm_stub_end
adl_smm_perm_stub_end:
