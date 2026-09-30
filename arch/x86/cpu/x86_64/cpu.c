// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Google, Inc
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <cpu_func.h>
#include <debug_uart.h>
#include <init.h>
#include <log.h>
#include <malloc.h>
#include <asm/cache.h>
#include <asm/control_regs.h>
#include <asm/cpu.h>
#include <asm/global_data.h>
#include <asm/processor.h>
#include <asm/processor-flags.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

/* Build a GDT entry; base is 32 bits and limit is 20 bits */
#define GDT_ENTRY(flags, base, limit)			\
	((((base)  & 0xff000000ULL) << (56 - 24)) |	\
	 (((flags) & 0x0000f0ffULL) << 40) |		\
	 (((limit) & 0x000f0000ULL) << (48 - 16)) |	\
	 (((base)  & 0x00ffffffULL) << 16) |		\
	 (((limit) & 0x0000ffffULL)))

struct gdt_ptr {
	u16 len;
	u64 ptr;
} __packed;

static void load_gdt(const u64 *gdt_addr, u16 num_entries)
{
	struct gdt_ptr gdt;

	gdt.len = (num_entries * X86_GDT_ENTRY_SIZE) - 1;
	gdt.ptr = (ulong)gdt_addr;

	asm volatile("lgdt %0\n" : : "m" (gdt));
}

static void load_ds(u32 segment)
{
	asm volatile("movl %0, %%ds" : : "r" (segment * X86_GDT_ENTRY_SIZE));
}

static void load_es(u32 segment)
{
	asm volatile("movl %0, %%es" : : "r" (segment * X86_GDT_ENTRY_SIZE));
}

static void load_fs(u32 segment)
{
	asm volatile("movl %0, %%fs" : : "r" (segment * X86_GDT_ENTRY_SIZE));
}

static void load_gs(u32 segment)
{
	asm volatile("movl %0, %%gs" : : "r" (segment * X86_GDT_ENTRY_SIZE));
}

static void load_ss(u32 segment)
{
	asm volatile("movl %0, %%ss" : : "r" (segment * X86_GDT_ENTRY_SIZE));
}

/**
 * arch_setup_gd() - Set up a GDT in the new global data and switch to it
 *
 * Until now U-Boot has been using whatever GDT it was started with, typically
 * the one SPL left in its own (now dead) global data in low memory. That GDT
 * has an FS entry whose base points at SPL's gd pointer, not ours.
 *
 * The gd pointer is normally found through MSR_FS_BASE, but any reload of the
 * FS segment register replaces that base with the one in the GDT entry. The
 * Windows boot manager does exactly this every time it switches between its
 * own execution context and the firmware's, so without a matching GDT entry the
 * first boot-services call after a switch reads a stale pointer and faults.
 *
 * So build the standard U-Boot GDT in the new gd, with FS pointing at the new
 * gd pointer, load it and reload the segment registers from it. The code
 * segment stays at selector 0x48, which is the same entry in both GDTs.
 *
 * @new_gd: New global data to point to
 */
void arch_setup_gd(gd_t *new_gd)
{
	u64 *gdt_addr = new_gd->arch.gdt;
	ulong addr;

	new_gd->arch.gd_addr = new_gd;
	addr = (ulong)&new_gd->arch.gd_addr;

	/* A GDT entry only has a 32-bit base, so FS cannot reach above 4GB */
	if (addr >> 32)
		log_warning("gd is above 4GB, so an FS reload will lose it\n");

	gdt_addr[X86_GDT_ENTRY_NULL] = 0;
	gdt_addr[X86_GDT_ENTRY_UNUSED] = GDT_ENTRY(0xc09b, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_32BIT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_32BIT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_32BIT_FS] = GDT_ENTRY(0x8093, addr,
						     sizeof(new_gd->arch.gd_addr) - 1);
	gdt_addr[X86_GDT_ENTRY_16BIT_CS] = GDT_ENTRY(0x009b, 0, 0x0ffff);
	gdt_addr[X86_GDT_ENTRY_16BIT_DS] = GDT_ENTRY(0x0093, 0, 0x0ffff);
	gdt_addr[X86_GDT_ENTRY_16BIT_FLAT_CS] = GDT_ENTRY(0x809b, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_16BIT_FLAT_DS] = GDT_ENTRY(0x8093, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_64BIT_CS] = GDT_ENTRY(0xaf9b, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_64BIT_TS1] = GDT_ENTRY(0x8980, 0, 0xfffff);
	gdt_addr[X86_GDT_ENTRY_64BIT_TS2] = 0;

	load_gdt(gdt_addr, X86_GDT_NUM_ENTRIES);
	load_ds(X86_GDT_ENTRY_32BIT_DS);
	load_es(X86_GDT_ENTRY_32BIT_DS);
	load_gs(X86_GDT_ENTRY_32BIT_DS);
	load_ss(X86_GDT_ENTRY_32BIT_DS);
	load_fs(X86_GDT_ENTRY_32BIT_FS);

	/* Loading FS set its base from the GDT; set the MSR too, to be sure */
	set_gd(new_gd);
}

int cpu_has_64bit(void)
{
	return true;
}

void enable_caches(void)
{
	unsigned long cr0;

	cr0 = read_cr0();
	cr0 &= ~(X86_CR0_NW | X86_CR0_CD);
	write_cr0(cr0);
}

void disable_caches(void)
{
	unsigned long cr0;

	cr0 = read_cr0();
	cr0 |= X86_CR0_NW | X86_CR0_CD;
	wbinvd();
	write_cr0(cr0);
	wbinvd();
}

int dcache_status(void)
{
	return !(read_cr0() & X86_CR0_CD);
}

int x86_mp_init(void)
{
	/* Not implemented */
	return 0;
}

/* enable SSE features for hardware floating point */
static void setup_sse_features(void)
{
	asm ("mov %%cr4, %%rax\n" \
	"or  %0, %%rax\n" \
	"mov %%rax, %%cr4\n" \
	: : "i" (X86_CR4_OSFXSR | X86_CR4_OSXMMEXCPT) : "eax");
}

int x86_cpu_reinit_f(void)
{
	x86_setup_identity_cpuid();
	if (IS_ENABLED(CONFIG_X86_HARDFP))
		setup_sse_features();

	return 0;
}

int x86_cpu_init_f(void)
{
	x86_setup_identity_cpuid();

	return 0;
}

#if defined(CONFIG_DEBUG_UART_BOARD_INIT) && defined(CONFIG_SPL)
/* SPL has already set up the debug UART; a board without SPL does its own */
void board_debug_uart_init(void)
{
}
#endif

void x86_get_identity_for_timer(void)
{
	x86_setup_identity_cpuid();
}

/* Page-table entry bits */
#define PTE_PRESENT		BIT(0)
#define PTE_RW			BIT(1)
#define PTE_PCD			BIT(4)	/* uncached */
#define PTE_PS			BIT(7)	/* large page */
#define PTE_ADDR_MASK		GENMASK_ULL(51, 12)
#define PAGE_TABLE_ENTRIES	512

int x86_64_map_mmio(u64 start, u64 size)
{
	u64 *pml4 = (u64 *)(read_cr3() & PTE_ADDR_MASK);
	u64 addr;

	if (!(cpuid_edx(0x80000001) & BIT(26)))
		return -EOPNOTSUPP;
	for (addr = ALIGN_DOWN(start, SZ_1G); addr < start + size;
	     addr += SZ_1G) {
		u64 *pml4e = &pml4[(addr >> 39) % PAGE_TABLE_ENTRIES];
		u64 *pdpt;

		if (!(*pml4e & PTE_PRESENT)) {
			pdpt = memalign(SZ_4K, SZ_4K);
			if (!pdpt)
				return -ENOMEM;
			memset(pdpt, '\0', SZ_4K);
			*pml4e = (ulong)pdpt | PTE_RW | PTE_PRESENT;
		}
		pdpt = (u64 *)(ulong)(*pml4e & PTE_ADDR_MASK);
		pdpt[(addr >> 30) % PAGE_TABLE_ENTRIES] = addr | PTE_PS |
			PTE_PCD | PTE_RW | PTE_PRESENT;
	}

	/* flush the TLB */
	asm volatile("mov %0, %%cr3" : : "r" (read_cr3()) : "memory");

	return 0;
}
