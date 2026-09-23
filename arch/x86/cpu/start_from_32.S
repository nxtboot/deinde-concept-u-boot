/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * 64-bit x86 Startup Code with integrated 32-bit init
 *
 * Entry point _start is .code32, called from start16.S after the
 * 16-to-32-bit transition.  This sets up an identity-mapped page table
 * and transitions to 64-bit mode before calling into the normal
 * board_init_f() flow.
 *
 * The 32-bit section uses position-independent code (call/pop for the
 * instruction pointer) because the 64-bit binary is linked as PIE.
 *
 * Copyright 2026 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <config.h>
#include <asm/msr-index.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>

/*
 * Page-table base address - must be 4KB aligned and below 4GB.
 * Uses 24KB total: PML4 (4KB) + PDPT (4KB) + 4 PD tables (4KB each)
 */
#define PT_BASE		X86_PAGETABLE_BASE

/* ------------------------------------------------------------------ */

.section .text.start
.code32
.globl _start
.type _start, @function
_start:
	/* Load the segment registers to match the GDT loaded in start16.S */
	movl	$(X86_GDT_ENTRY_32BIT_DS * X86_GDT_ENTRY_SIZE), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %gs
	movl	%eax, %ss

	/* Set up the stack in the CAR/SRAM region */
	movl	$(CONFIG_SYS_CAR_ADDR + CONFIG_SYS_CAR_SIZE - 4), %esp

	/* Clear IDT */
	subl	$8, %esp
	movl	$0, 4(%esp)		/* base = 0 */
	movw	$0, 2(%esp)		/* padding */
	movw	$0, (%esp)		/* limit = 0 */
	lidt	(%esp)
	addl	$8, %esp

	/*
	 * Get our runtime address into %ebx so we can reference data
	 * position-independently (the 64-bit binary is linked as PIE)
	 */
	call	2f
2:	popl	%ebx

	/*
	 * Copy the boot GDT from ROM to RAM and load it from there.
	 * KVM's EPT may not allow data reads from the ROM region, so
	 * the GDT must be in RAM for the far jump to read the 64-bit
	 * CS descriptor.
	 */
#define GDT_RAM		0x2000
	leal	(boot_gdt - 2b)(%ebx), %esi
	movl	$GDT_RAM, %edi
	movl	$((boot_gdt_end - boot_gdt) / 4), %ecx
	cld
	rep movsl

	subl	$8, %esp
	movl	$GDT_RAM, 2(%esp)	/* base in RAM */
	movw	$(boot_gdt_end - boot_gdt - 1), (%esp)	/* limit */
	lgdt	(%esp)
	addl	$8, %esp

	/*
	 * Build identity-mapped page tables at PT_BASE (maps 4GB with
	 * 2MB pages).  This is similar to build_pagetable() in
	 * arch/x86/cpu/i386/cpu.c (which also sets the US/A/DT bits)
	 * but must be done in assembly because page tables are needed
	 * to enter 64-bit mode and all C code in this build is compiled
	 * for 64-bit.
	 *
	 * Layout (24KB total):
	 *   PT_BASE + 0x0000  PML4           (512 entries, only [0] used)
	 *   PT_BASE + 0x1000  PDPT           (512 entries, [0]..[3] used)
	 *   PT_BASE + 0x2000  PD for 0-1GB   (512 * 2MB entries)
	 *   PT_BASE + 0x3000  PD for 1-2GB
	 *   PT_BASE + 0x4000  PD for 2-3GB
	 *   PT_BASE + 0x5000  PD for 3-4GB
	 */

	/* Zero 24KB */
	movl	$PT_BASE, %edi
	xorl	%eax, %eax
	movl	$(X86_PAGETABLE_SIZE / 4), %ecx
	rep stosl

	/* PML4[0] -> PDPT */
	movl	$(PT_BASE + 0x1000 + 0x03), %eax	/* Present + RW */
	movl	%eax, PT_BASE

	/* PDPT[0..3] -> four PD tables */
	movl	$(PT_BASE + 0x2000 + 0x03), %eax
	movl	%eax, (PT_BASE + 0x1000 + 0 * 8)
	addl	$0x1000, %eax
	movl	%eax, (PT_BASE + 0x1000 + 1 * 8)
	addl	$0x1000, %eax
	movl	%eax, (PT_BASE + 0x1000 + 2 * 8)
	addl	$0x1000, %eax
	movl	%eax, (PT_BASE + 0x1000 + 3 * 8)

	/*
	 * Fill the four PD tables (2048 entries total).
	 * Each entry maps a 2MB page: address | PS(bit7) | RW | P
	 */
	movl	$(PT_BASE + 0x2000), %edi
	movl	$0x00000083, %eax		/* 0MB, PS + RW + P */
	movl	$2048, %ecx
1:
	movl	%eax, (%edi)
	movl	$0, 4(%edi)			/* high 32 bits = 0 */
	addl	$0x200000, %eax			/* next 2MB page */
	addl	$8, %edi
	decl	%ecx
	jnz	1b

	/*
	 * Transition to 64-bit long mode.  This is similar to
	 * cpu_call64() in arch/x86/cpu/i386/call64.S but uses lret
	 * instead of ljmp (which would emit a PIE-incompatible
	 * relocation).  It also enables SSE which call64.S does not
	 * need to do.
	 */

	/* Disable paging (should already be off after reset) */
	movl	%cr0, %eax
	andl	$~X86_CR0_PG, %eax
	movl	%eax, %cr0

	/* Enable PAE and SSE (x86_64 gcc assumes SSE2 is available) */
	movl	%cr4, %eax
	orl	$(X86_CR4_PAE | X86_CR4_OSFXSR), %eax
	movl	%eax, %cr4

	/* Clear CR0.EM so SSE instructions do not fault */
	movl	%cr0, %eax
	andl	$~X86_CR0_EM, %eax
	movl	%eax, %cr0

	/* Point CR3 at PML4 */
	movl	$PT_BASE, %eax
	movl	%eax, %cr3

	/* Enable Long Mode in EFER */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax
	wrmsr

	/* Enable paging -> activates long mode */
	movl	%cr0, %eax
	orl	$X86_CR0_PG, %eax
	movl	%eax, %cr0

	/*
	 * Jump to 64-bit code segment.  Use lret to avoid the
	 * PIE-incompatible relocation that a direct ljmp would emit.
	 */
	leal	(start64 - 2b)(%ebx), %eax
	pushl	$(X86_GDT_ENTRY_64BIT_CS * X86_GDT_ENTRY_SIZE)
	pushl	%eax
	lret

/* ------------------------------------------------------------------ */
.code64
start64:
	/* Set up memory using the existing stack */
	mov	%rsp, %rdi
	call	board_init_f_alloc_reserve
	mov	%rax, %rsp
	mov	%rsp, %rdi
	call	board_init_f_init_reserve

	xor	%rdi, %rdi
	call	board_init_f
	call	board_init_f_r

	/* Should not return here */
	jmp	.

.globl board_init_f_r_trampoline64
.type board_init_f_r_trampoline64, @function
board_init_f_r_trampoline64:
	/*
	 * SDRAM has been initialised, U-Boot code has been copied into
	 * RAM, BSS has been cleared and relocation adjustments have been
	 * made. It is now time to jump into the in-RAM copy of U-Boot
	 *
	 * %rsi = Address of top of new stack
	 * %rdi = New gd
	 */

	/* Stack grows down from top of SDRAM */
	movq	%rsi, %rsp

	/* Re-enter U-Boot by calling board_init_f_r() */
	call	board_init_f_r

/* ------------------------------------------------------------------ */
/* Data */
/* ------------------------------------------------------------------ */

/*
 * Boot GDT - includes valid 32-bit CS/DS entries (matching start16.S's
 * selectors 0x10 and 0x18) plus the 64-bit CS at entry 9 (selector
 * 0x48, matching U-Boot's standard GDT numbering).
 *
 * This is copied to RAM before use because KVM cannot perform the
 * implicit GDT data read from the ROM region during the far jump
 * to 64-bit mode.
 *
 * When arch_setup_gd() later loads the real GDT the CS selector (0x48)
 * remains valid.
 */
.align 16
boot_gdt:
	/* Entry 0: NULL */
	.quad	0
	/* Entry 1: unused (matches start16.S layout) */
	.quad	0
	/* Entry 2: 32-bit code segment (selector 0x10) */
	.quad	0x00cf9b000000ffff
	/* Entry 3: 32-bit data segment (selector 0x18) */
	.quad	0x00cf93000000ffff
	/* Entries 4-8: unused */
	.fill	5, 8, 0

	/* Entry 9: 64-bit code segment (selector 0x48) */
	.quad	0x00af9a000000ffff

	/* Entry 10-11: unused (keep GDT same size as real one) */
	.quad	0
	.quad	0
boot_gdt_end:
