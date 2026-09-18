// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 *
 * 64-bit exception and interrupt handling. This mirrors the 32-bit code in
 * arch/x86/cpu/i386/interrupt.c but with 16-byte IDT gates and a uniform
 * stack frame: the vector stub pushes a dummy error code for exceptions
 * which do not provide one, so every handler sees the same layout.
 */

#include <efi_loader.h>
#include <hang.h>
#include <init.h>
#include <irq_func.h>
#include <asm/control_regs.h>
#include <asm/global_data.h>
#include <asm/interrupt.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>
#include <linux/stringify.h>

DECLARE_GLOBAL_DATA_PTR;

/* Each stub is padded to this size so that the IDT can be filled in a loop */
#define IRQ_STUB_SIZE	16

/* Exceptions which do not push an error code get a dummy one from the stub */
#define DECLARE_INTERRUPT(x) \
	".balign " __stringify(IRQ_STUB_SIZE) "\n" \
	".globl irq_" #x "\n" \
	".hidden irq_" #x "\n" \
	".type irq_" #x ", @function\n" \
	"irq_" #x ":\n" \
	"pushq $0\n" \
	"pushq $" #x "\n" \
	"jmp.d32 irq_common_entry\n"

/* Exceptions which push an error code themselves */
#define DECLARE_INTERRUPT_ERR(x) \
	".balign " __stringify(IRQ_STUB_SIZE) "\n" \
	".globl irq_" #x "\n" \
	".hidden irq_" #x "\n" \
	".type irq_" #x ", @function\n" \
	"irq_" #x ":\n" \
	"pushq $" #x "\n" \
	"jmp.d32 irq_common_entry\n"

/**
 * struct irq_regs64 - registers saved on the stack by irq_common_entry
 *
 * The CPU pushes ss, rsp, rflags, cs and rip, then (for some exceptions) an
 * error code. The vector stub pushes the dummy error code if needed, then the
 * vector number. The common entry then pushes the general-purpose registers.
 */
struct irq_regs64 {
	u64 r15;
	u64 r14;
	u64 r13;
	u64 r12;
	u64 r11;
	u64 r10;
	u64 r9;
	u64 r8;
	u64 rdi;
	u64 rsi;
	u64 rbp;
	u64 rbx;
	u64 rdx;
	u64 rcx;
	u64 rax;
	u64 irq_id;
	u64 err;
	u64 rip;
	u64 cs;
	u64 rflags;
	u64 rsp;
	u64 ss;
};

static const char *const exceptions[] = {
	"Divide Error",
	"Debug",
	"NMI Interrupt",
	"Breakpoint",
	"Overflow",
	"BOUND Range Exceeded",
	"Invalid Opcode (Undefined Opcode)",
	"Device Not Available (No Math Coprocessor)",
	"Double Fault",
	"Coprocessor Segment Overrun",
	"Invalid TSS",
	"Segment Not Present",
	"Stack Segment Fault",
	"General Protection",
	"Page Fault",
	"Reserved",
	"x87 FPU Floating-Point Error",
	"Alignment Check",
	"Machine Check",
	"SIMD Floating-Point Exception",
	"Virtualization Exception",
	"Control Protection Exception",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

static void dump_regs(struct irq_regs64 *regs)
{
	u64 *sp;
	int i;

	printf("RIP: %04llx:[<%016llx>] RFLAGS: %08llx err: %llx\n",
	       regs->cs, regs->rip, regs->rflags, regs->err);
	if (gd->flags & GD_FLG_RELOC)
		printf("Original RIP :[<%016llx>]\n", regs->rip - gd->reloc_off);

	printf("RAX: %016llx RBX: %016llx RCX: %016llx RDX: %016llx\n",
	       regs->rax, regs->rbx, regs->rcx, regs->rdx);
	printf("RSI: %016llx RDI: %016llx RBP: %016llx RSP: %016llx\n",
	       regs->rsi, regs->rdi, regs->rbp, regs->rsp);
	printf("R8 : %016llx R9 : %016llx R10: %016llx R11: %016llx\n",
	       regs->r8, regs->r9, regs->r10, regs->r11);
	printf("R12: %016llx R13: %016llx R14: %016llx R15: %016llx\n",
	       regs->r12, regs->r13, regs->r14, regs->r15);
	printf(" SS: %04llx\n", regs->ss);
	printf("CR0: %016lx CR2: %016lx CR3: %016lx CR4: %016lx\n",
	       read_cr0(), read_cr2(), read_cr3(), read_cr4());

	printf("Stack:\n");
	sp = (u64 *)regs->rsp;
	for (i = 0; i < 16; i++)
		printf("    %016lx : %016llx\n", (ulong)&sp[i], sp[i]);
	efi_print_image_infos((void *)regs->rip);
}

static void do_exception(struct irq_regs64 *regs)
{
	printf("%s\n", exceptions[regs->irq_id]);
	dump_regs(regs);
	hang();
}

/* 64-bit IDT gate */
struct idt_entry {
	u16	base_low;
	u16	selector;
	u8	ist;
	u8	access;
	u16	base_mid;
	u32	base_high;
	u32	reserved;
} __packed;

static struct idt_entry idt[256] __aligned(16);

static struct idt_ptr idt_ptr;

static inline void load_idt(const struct idt_ptr *dtr)
{
	asm volatile("lidt %0" : : "m" (*dtr));
}

void set_vector(u8 intnum, void *routine)
{
	ulong addr = (ulong)routine;

	idt[intnum].base_low = addr & 0xffff;
	idt[intnum].base_mid = (addr >> 16) & 0xffff;
	idt[intnum].base_high = addr >> 32;
}

/*
 * Ideally these would be defined static to avoid a checkpatch warning, but
 * the compiler cannot see them in the inline asm and complains that they
 * aren't defined
 */
void irq_0(void);
void irq_1(void);

int cpu_init_interrupts(void)
{
	void *irq_entry = (void *)irq_0;
	int i;

	/* Set up the IDT */
	for (i = 0; i < 256; i++) {
		idt[i].access = 0x8e;
		idt[i].ist = 0;
		idt[i].selector = X86_GDT_ENTRY_64BIT_CS * X86_GDT_ENTRY_SIZE;
		set_vector(i, irq_entry);
		irq_entry += IRQ_STUB_SIZE;
	}

	idt_ptr.size = sizeof(idt) - 1;
	idt_ptr.address = (ulong)idt;

	load_idt(&idt_ptr);

	return 0;
}

void interrupt_read_idt(struct idt_ptr *ptr)
{
	asm volatile("sidt %0" : : "m" (*ptr));
}

void *x86_get_idt(void)
{
	return &idt_ptr;
}

void enable_interrupts(void)
{
	asm("sti\n");
}

int disable_interrupts(void)
{
	long flags;

	asm volatile ("pushfq ; popq %0 ; cli\n" : "=g" (flags) : );

	return flags & X86_EFLAGS_IF;
}

int interrupt_init(void)
{
	/*
	 * When running as an EFI application we are not in control of
	 * interrupts and should leave them alone.
	 */
	if (!ll_boot_init())
		return 0;

	/* The interrupt controllers were set up by SPL; just add the IDT */
	disable_interrupts();
	cpu_init_interrupts();
	enable_interrupts();

	return 0;
}

/* IRQ Low-Level Service Routine */
void irq_llsr(struct irq_regs64 *regs)
{
	if (regs->irq_id < 32)
		do_exception(regs);
	else
		do_irq(regs->irq_id);
}

/*
 * Interrupt entry point: the CPU has pushed ss, rsp, rflags, cs, rip and
 * possibly an error code; the vector stub has made sure there is always an
 * error code slot and pushed the vector number. Save the general-purpose
 * registers, call irq_llsr() with a pointer to the frame, then restore
 * everything and return.
 */
asm(".globl irq_common_entry\n" \
	".hidden irq_common_entry\n" \
	".type irq_common_entry, @function\n" \
	"irq_common_entry:\n" \
	"cld\n" \
	"pushq %rax\n" \
	"pushq %rcx\n" \
	"pushq %rdx\n" \
	"pushq %rbx\n" \
	"pushq %rbp\n" \
	"pushq %rsi\n" \
	"pushq %rdi\n" \
	"pushq %r8\n" \
	"pushq %r9\n" \
	"pushq %r10\n" \
	"pushq %r11\n" \
	"pushq %r12\n" \
	"pushq %r13\n" \
	"pushq %r14\n" \
	"pushq %r15\n" \
	"mov   %rsp, %rdi\n" \
	"call  irq_llsr\n" \
	"popq %r15\n" \
	"popq %r14\n" \
	"popq %r13\n" \
	"popq %r12\n" \
	"popq %r11\n" \
	"popq %r10\n" \
	"popq %r9\n" \
	"popq %r8\n" \
	"popq %rdi\n" \
	"popq %rsi\n" \
	"popq %rbp\n" \
	"popq %rbx\n" \
	"popq %rdx\n" \
	"popq %rcx\n" \
	"popq %rax\n" \
	"add  $16, %rsp\n" \
	"iretq\n" \
	DECLARE_INTERRUPT(0) \
	DECLARE_INTERRUPT(1) \
	DECLARE_INTERRUPT(2) \
	DECLARE_INTERRUPT(3) \
	DECLARE_INTERRUPT(4) \
	DECLARE_INTERRUPT(5) \
	DECLARE_INTERRUPT(6) \
	DECLARE_INTERRUPT(7) \
	DECLARE_INTERRUPT_ERR(8) \
	DECLARE_INTERRUPT(9) \
	DECLARE_INTERRUPT_ERR(10) \
	DECLARE_INTERRUPT_ERR(11) \
	DECLARE_INTERRUPT_ERR(12) \
	DECLARE_INTERRUPT_ERR(13) \
	DECLARE_INTERRUPT_ERR(14) \
	DECLARE_INTERRUPT(15) \
	DECLARE_INTERRUPT(16) \
	DECLARE_INTERRUPT_ERR(17) \
	DECLARE_INTERRUPT(18) \
	DECLARE_INTERRUPT(19) \
	DECLARE_INTERRUPT(20) \
	DECLARE_INTERRUPT_ERR(21) \
	DECLARE_INTERRUPT(22) \
	DECLARE_INTERRUPT(23) \
	DECLARE_INTERRUPT(24) \
	DECLARE_INTERRUPT(25) \
	DECLARE_INTERRUPT(26) \
	DECLARE_INTERRUPT(27) \
	DECLARE_INTERRUPT(28) \
	DECLARE_INTERRUPT_ERR(29) \
	DECLARE_INTERRUPT_ERR(30) \
	DECLARE_INTERRUPT(31) \
	DECLARE_INTERRUPT(32) \
	DECLARE_INTERRUPT(33) \
	DECLARE_INTERRUPT(34) \
	DECLARE_INTERRUPT(35) \
	DECLARE_INTERRUPT(36) \
	DECLARE_INTERRUPT(37) \
	DECLARE_INTERRUPT(38) \
	DECLARE_INTERRUPT(39) \
	DECLARE_INTERRUPT(40) \
	DECLARE_INTERRUPT(41) \
	DECLARE_INTERRUPT(42) \
	DECLARE_INTERRUPT(43) \
	DECLARE_INTERRUPT(44) \
	DECLARE_INTERRUPT(45) \
	DECLARE_INTERRUPT(46) \
	DECLARE_INTERRUPT(47) \
	DECLARE_INTERRUPT(48) \
	DECLARE_INTERRUPT(49) \
	DECLARE_INTERRUPT(50) \
	DECLARE_INTERRUPT(51) \
	DECLARE_INTERRUPT(52) \
	DECLARE_INTERRUPT(53) \
	DECLARE_INTERRUPT(54) \
	DECLARE_INTERRUPT(55) \
	DECLARE_INTERRUPT(56) \
	DECLARE_INTERRUPT(57) \
	DECLARE_INTERRUPT(58) \
	DECLARE_INTERRUPT(59) \
	DECLARE_INTERRUPT(60) \
	DECLARE_INTERRUPT(61) \
	DECLARE_INTERRUPT(62) \
	DECLARE_INTERRUPT(63) \
	DECLARE_INTERRUPT(64) \
	DECLARE_INTERRUPT(65) \
	DECLARE_INTERRUPT(66) \
	DECLARE_INTERRUPT(67) \
	DECLARE_INTERRUPT(68) \
	DECLARE_INTERRUPT(69) \
	DECLARE_INTERRUPT(70) \
	DECLARE_INTERRUPT(71) \
	DECLARE_INTERRUPT(72) \
	DECLARE_INTERRUPT(73) \
	DECLARE_INTERRUPT(74) \
	DECLARE_INTERRUPT(75) \
	DECLARE_INTERRUPT(76) \
	DECLARE_INTERRUPT(77) \
	DECLARE_INTERRUPT(78) \
	DECLARE_INTERRUPT(79) \
	DECLARE_INTERRUPT(80) \
	DECLARE_INTERRUPT(81) \
	DECLARE_INTERRUPT(82) \
	DECLARE_INTERRUPT(83) \
	DECLARE_INTERRUPT(84) \
	DECLARE_INTERRUPT(85) \
	DECLARE_INTERRUPT(86) \
	DECLARE_INTERRUPT(87) \
	DECLARE_INTERRUPT(88) \
	DECLARE_INTERRUPT(89) \
	DECLARE_INTERRUPT(90) \
	DECLARE_INTERRUPT(91) \
	DECLARE_INTERRUPT(92) \
	DECLARE_INTERRUPT(93) \
	DECLARE_INTERRUPT(94) \
	DECLARE_INTERRUPT(95) \
	DECLARE_INTERRUPT(96) \
	DECLARE_INTERRUPT(97) \
	DECLARE_INTERRUPT(98) \
	DECLARE_INTERRUPT(99) \
	DECLARE_INTERRUPT(100) \
	DECLARE_INTERRUPT(101) \
	DECLARE_INTERRUPT(102) \
	DECLARE_INTERRUPT(103) \
	DECLARE_INTERRUPT(104) \
	DECLARE_INTERRUPT(105) \
	DECLARE_INTERRUPT(106) \
	DECLARE_INTERRUPT(107) \
	DECLARE_INTERRUPT(108) \
	DECLARE_INTERRUPT(109) \
	DECLARE_INTERRUPT(110) \
	DECLARE_INTERRUPT(111) \
	DECLARE_INTERRUPT(112) \
	DECLARE_INTERRUPT(113) \
	DECLARE_INTERRUPT(114) \
	DECLARE_INTERRUPT(115) \
	DECLARE_INTERRUPT(116) \
	DECLARE_INTERRUPT(117) \
	DECLARE_INTERRUPT(118) \
	DECLARE_INTERRUPT(119) \
	DECLARE_INTERRUPT(120) \
	DECLARE_INTERRUPT(121) \
	DECLARE_INTERRUPT(122) \
	DECLARE_INTERRUPT(123) \
	DECLARE_INTERRUPT(124) \
	DECLARE_INTERRUPT(125) \
	DECLARE_INTERRUPT(126) \
	DECLARE_INTERRUPT(127) \
	DECLARE_INTERRUPT(128) \
	DECLARE_INTERRUPT(129) \
	DECLARE_INTERRUPT(130) \
	DECLARE_INTERRUPT(131) \
	DECLARE_INTERRUPT(132) \
	DECLARE_INTERRUPT(133) \
	DECLARE_INTERRUPT(134) \
	DECLARE_INTERRUPT(135) \
	DECLARE_INTERRUPT(136) \
	DECLARE_INTERRUPT(137) \
	DECLARE_INTERRUPT(138) \
	DECLARE_INTERRUPT(139) \
	DECLARE_INTERRUPT(140) \
	DECLARE_INTERRUPT(141) \
	DECLARE_INTERRUPT(142) \
	DECLARE_INTERRUPT(143) \
	DECLARE_INTERRUPT(144) \
	DECLARE_INTERRUPT(145) \
	DECLARE_INTERRUPT(146) \
	DECLARE_INTERRUPT(147) \
	DECLARE_INTERRUPT(148) \
	DECLARE_INTERRUPT(149) \
	DECLARE_INTERRUPT(150) \
	DECLARE_INTERRUPT(151) \
	DECLARE_INTERRUPT(152) \
	DECLARE_INTERRUPT(153) \
	DECLARE_INTERRUPT(154) \
	DECLARE_INTERRUPT(155) \
	DECLARE_INTERRUPT(156) \
	DECLARE_INTERRUPT(157) \
	DECLARE_INTERRUPT(158) \
	DECLARE_INTERRUPT(159) \
	DECLARE_INTERRUPT(160) \
	DECLARE_INTERRUPT(161) \
	DECLARE_INTERRUPT(162) \
	DECLARE_INTERRUPT(163) \
	DECLARE_INTERRUPT(164) \
	DECLARE_INTERRUPT(165) \
	DECLARE_INTERRUPT(166) \
	DECLARE_INTERRUPT(167) \
	DECLARE_INTERRUPT(168) \
	DECLARE_INTERRUPT(169) \
	DECLARE_INTERRUPT(170) \
	DECLARE_INTERRUPT(171) \
	DECLARE_INTERRUPT(172) \
	DECLARE_INTERRUPT(173) \
	DECLARE_INTERRUPT(174) \
	DECLARE_INTERRUPT(175) \
	DECLARE_INTERRUPT(176) \
	DECLARE_INTERRUPT(177) \
	DECLARE_INTERRUPT(178) \
	DECLARE_INTERRUPT(179) \
	DECLARE_INTERRUPT(180) \
	DECLARE_INTERRUPT(181) \
	DECLARE_INTERRUPT(182) \
	DECLARE_INTERRUPT(183) \
	DECLARE_INTERRUPT(184) \
	DECLARE_INTERRUPT(185) \
	DECLARE_INTERRUPT(186) \
	DECLARE_INTERRUPT(187) \
	DECLARE_INTERRUPT(188) \
	DECLARE_INTERRUPT(189) \
	DECLARE_INTERRUPT(190) \
	DECLARE_INTERRUPT(191) \
	DECLARE_INTERRUPT(192) \
	DECLARE_INTERRUPT(193) \
	DECLARE_INTERRUPT(194) \
	DECLARE_INTERRUPT(195) \
	DECLARE_INTERRUPT(196) \
	DECLARE_INTERRUPT(197) \
	DECLARE_INTERRUPT(198) \
	DECLARE_INTERRUPT(199) \
	DECLARE_INTERRUPT(200) \
	DECLARE_INTERRUPT(201) \
	DECLARE_INTERRUPT(202) \
	DECLARE_INTERRUPT(203) \
	DECLARE_INTERRUPT(204) \
	DECLARE_INTERRUPT(205) \
	DECLARE_INTERRUPT(206) \
	DECLARE_INTERRUPT(207) \
	DECLARE_INTERRUPT(208) \
	DECLARE_INTERRUPT(209) \
	DECLARE_INTERRUPT(210) \
	DECLARE_INTERRUPT(211) \
	DECLARE_INTERRUPT(212) \
	DECLARE_INTERRUPT(213) \
	DECLARE_INTERRUPT(214) \
	DECLARE_INTERRUPT(215) \
	DECLARE_INTERRUPT(216) \
	DECLARE_INTERRUPT(217) \
	DECLARE_INTERRUPT(218) \
	DECLARE_INTERRUPT(219) \
	DECLARE_INTERRUPT(220) \
	DECLARE_INTERRUPT(221) \
	DECLARE_INTERRUPT(222) \
	DECLARE_INTERRUPT(223) \
	DECLARE_INTERRUPT(224) \
	DECLARE_INTERRUPT(225) \
	DECLARE_INTERRUPT(226) \
	DECLARE_INTERRUPT(227) \
	DECLARE_INTERRUPT(228) \
	DECLARE_INTERRUPT(229) \
	DECLARE_INTERRUPT(230) \
	DECLARE_INTERRUPT(231) \
	DECLARE_INTERRUPT(232) \
	DECLARE_INTERRUPT(233) \
	DECLARE_INTERRUPT(234) \
	DECLARE_INTERRUPT(235) \
	DECLARE_INTERRUPT(236) \
	DECLARE_INTERRUPT(237) \
	DECLARE_INTERRUPT(238) \
	DECLARE_INTERRUPT(239) \
	DECLARE_INTERRUPT(240) \
	DECLARE_INTERRUPT(241) \
	DECLARE_INTERRUPT(242) \
	DECLARE_INTERRUPT(243) \
	DECLARE_INTERRUPT(244) \
	DECLARE_INTERRUPT(245) \
	DECLARE_INTERRUPT(246) \
	DECLARE_INTERRUPT(247) \
	DECLARE_INTERRUPT(248) \
	DECLARE_INTERRUPT(249) \
	DECLARE_INTERRUPT(250) \
	DECLARE_INTERRUPT(251) \
	DECLARE_INTERRUPT(252) \
	DECLARE_INTERRUPT(253) \
	DECLARE_INTERRUPT(254) \
	DECLARE_INTERRUPT(255));
