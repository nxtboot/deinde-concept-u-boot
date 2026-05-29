// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 *
 * EFI information obtained here:
 * http://wiki.phoenix.com/wiki/index.php/EFI_BOOT_SERVICES
 *
 * Loads a payload (U-Boot) within the EFI environment. This is built as an
 * EFI application. It can be built either in 32-bit or 64-bit mode.
 */

#define LOG_CATEGORY	LOGC_EFI

#include <debug_uart.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_stub.h>
#include <errno.h>
#include <malloc.h>
#include <ns16550.h>
#include <asm/cpu.h>
#include <asm/io.h>
#include <linux/err.h>
#include <linux/types.h>

#ifndef CONFIG_X86
/*
 * Problem areas:
 * - putc() uses the ns16550 address directly and assumed I/O access. Many
 *	platforms will use memory access
 * get_codeseg32() is only meaningful on x86
 */
#error "This file needs to be ported for use on architectures"
#endif

struct __packed desctab_info {
	uint16_t limit;
	uint64_t addr;
	uint16_t pad;
};

/*
 * EFI uses Unicode and we don't. The easiest way to get a sensible output
 * function is to use the U-Boot debug UART. We use EFI's console output
 * function where available, and assume the built-in UART after that. We rely
 * on EFI to set up the UART for us and just bring in the functions here.
 * This last bit is a bit icky, but it's only for debugging anyway. We could
 * build in ns16550.c with some effort, but this is a payload loader after
 * all.
 *
 * Note: We avoid using printf() so we don't need to bring in lib/vsprintf.c.
 * That would require some refactoring since we already build this for U-Boot.
 * Building an EFI shared library version would have to be a separate stem.
 * That might push us to using the SPL framework to build this stub. However
 * that would involve a round of EFI-specific changes in SPL. Worth
 * considering if we start needing more U-Boot functionality. Note that we
 * could then move get_codeseg32() to arch/x86/cpu/cpu.c.
 */
void _debug_uart_init(void)
{
}

void putc(const char ch)
{
	struct efi_priv *priv = efi_get_priv();

	if (ch == '\n')
		putc('\r');

	if (use_hw_uart) {
		struct ns16550 *com_port = (struct ns16550 *)0x3f8;

		while ((inb((ulong)&com_port->lsr) & UART_LSR_THRE) == 0)
			;
		outb(ch, (ulong)&com_port->thr);
	} else {
		efi_putc(priv, ch);
	}
}

void puts(const char *str)
{
	while (*str)
		putc(*str++);
}

static inline void _debug_uart_putc(int ch)
{
	putc(ch);
}

DEBUG_UART_FUNCS

static void jump_to_uboot(ulong cs32, ulong addr, ulong info)
{
#ifdef CONFIG_EFI_STUB_32BIT
	/*
	 * U-Boot requires these parameters in registers, not on the stack.
	 * See _x86boot_start() for this code.
	 */
	typedef void (*func_t)(int bist, int unused, ulong info)
		__attribute__((regparm(3)));

	((func_t)addr)(0, 0, info);
#else
	cpu_call32(cs32, CONFIG_PPL_TEXT_BASE, info);
#endif
}

#ifdef CONFIG_EFI_STUB_64BIT
static void get_gdt(struct desctab_info *info)
{
	asm volatile ("sgdt %0" : : "m"(*info) : "memory");
}
#endif

static inline unsigned long read_cr3(void)
{
	unsigned long val;

	asm volatile("mov %%cr3,%0" : "=r" (val) : : "memory");
	return val;
}

/**
 * get_codeseg32() - Find the code segment to use for 32-bit code
 *
 * U-Boot only works in 32-bit mode at present, so when booting from 64-bit
 * EFI we must first change to 32-bit mode. To do this we need to find the
 * correct code segment to use (an entry in the Global Descriptor Table).
 *
 * Return: code segment GDT offset, or 0 for 32-bit EFI, -ENOENT if not found
 */
static int get_codeseg32(void)
{
	int cs32 = 0;

#ifdef CONFIG_EFI_STUB_64BIT
	struct desctab_info gdt;
	uint64_t *ptr;
	int i;

	get_gdt(&gdt);
	for (ptr = (uint64_t *)(unsigned long)gdt.addr, i = 0; i < gdt.limit;
	     i += 8, ptr++) {
		uint64_t desc = *ptr;
		uint64_t base, limit;

		/*
		 * Check that the target U-Boot jump address is within the
		 * selector and that the selector is of the right type.
		 */
		base = ((desc >> GDT_BASE_LOW_SHIFT) & GDT_BASE_LOW_MASK) |
			((desc >> GDT_BASE_HIGH_SHIFT) & GDT_BASE_HIGH_MASK)
				<< 16;
		limit = ((desc >> GDT_LIMIT_LOW_SHIFT) & GDT_LIMIT_LOW_MASK) |
			((desc >> GDT_LIMIT_HIGH_SHIFT) & GDT_LIMIT_HIGH_MASK)
				<< 16;
		base <<= 12;	/* 4KB granularity */
		limit <<= 12;
		if ((desc & GDT_PRESENT) && (desc & GDT_NOTSYS) &&
		    !(desc & GDT_LONG) && (desc & GDT_4KB) &&
		    (desc & GDT_32BIT) && (desc & GDT_CODE) &&
		    CONFIG_PPL_TEXT_BASE > base &&
		    CONFIG_PPL_TEXT_BASE + CONFIG_SYS_MONITOR_LEN < limit
		) {
			cs32 = i;
			break;
		}
	}

#ifdef DEBUG
	puts("\ngdt: ");
	printhex8(gdt.limit);
	puts(", addr: ");
	printhex8(gdt.addr >> 32);
	printhex8(gdt.addr);
	for (i = 0; i < gdt.limit; i += 8) {
		uint32_t *ptr = (uint32_t *)((unsigned long)gdt.addr + i);

		puts("\n");
		printhex2(i);
		puts(": ");
		printhex8(ptr[1]);
		puts("  ");
		printhex8(ptr[0]);
	}
	puts("\n ");
	puts("32-bit code segment: ");
	printhex2(cs32);
	puts("\n ");

	puts("page_table: ");
	printhex8(read_cr3());
	puts("\n ");
#endif
	if (!cs32) {
		puts("Can't find 32-bit code segment\n");
		return -ENOENT;
	}
#endif

	return cs32;
}

efi_status_t arch_efi_main_init(struct efi_priv *priv,
				struct efi_boot_services *boot)
{
	int cs32;

	cs32 = get_codeseg32();
	if (cs32 < 0)
		return EFI_UNSUPPORTED;
	priv->x86_cs32 = cs32;

	priv->jump_addr = CONFIG_PPL_TEXT_BASE;

	return 0;
}

void arch_efi_jump_to_payload(struct efi_priv *priv)
{
	jump_to_uboot(priv->x86_cs32, priv->jump_addr, (ulong)priv->info);
}
