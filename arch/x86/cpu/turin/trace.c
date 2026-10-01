// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Register-write tracing through the debug UART, which works before the
 * console does. The lines match those of openSIL with its trace hooks
 */

#include <debug_uart.h>
#include <asm/arch/trace.h>

static void hex(ulong val)
{
	if (val >> 32)
		printhex8(val >> 32);
	printhex8(val);
}

/* Print the width of an access in decimal, as openSIL does */
static void bits_out(int bits)
{
	if (bits >= 10)
		printch('0' + bits / 10);
	printch('0' + bits % 10);
}

void turin_trace_smn(u32 addr, u32 val)
{
	printascii("T S 0 ");
	printhex8(addr);
	printch(' ');
	printhex8(val);
	printch('\n');
}

void turin_trace_pci(pci_dev_t bdf, uint offset, ulong val, int bits)
{
	printascii("T P");
	bits_out(bits);
	printch(' ');
	printhex2(PCI_BUS(bdf));
	printch(':');
	printhex2(PCI_DEV(bdf));
	printch('.');
	printch('0' + PCI_FUNC(bdf));
	printch(' ');
	printhex4(offset);
	printch(' ');
	hex(val);
	printch('\n');
}

void turin_trace_msr(u32 msr, u64 val)
{
	printascii("T R ");
	printhex8(msr);
	printch(' ');
	hex(val);
	printch('\n');
}

void turin_trace_mmio(ulong addr, ulong val, int bits)
{
	printascii("T M");
	bits_out(bits);
	printch(' ');
	hex(addr);
	printch(' ');
	hex(val);
	printch('\n');
}

static void args_out(const u32 *args, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		printch(' ');
		printhex8(args[i]);
	}
	printch('\n');
}

void turin_trace_msg(char kind, u32 msg, const u32 *args, int count)
{
	printascii("T ");
	printch(kind);
	printascii(" 0 ");
	printhex8(msg);
	args_out(args, count);
}

void turin_trace_resp(char kind, u32 resp, const u32 *args, int count)
{
	printascii("T ");
	printch(kind);
	printch(' ');
	printhex8(resp);
	args_out(args, count);
}
