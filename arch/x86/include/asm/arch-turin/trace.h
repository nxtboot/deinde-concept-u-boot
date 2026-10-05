/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Tracing of the register writes and firmware messages which bring up the
 * processor, in the form openSIL prints them when given the same hooks, so
 * that the two register programs can be compared
 */

#ifndef __ASM_ARCH_TRACE_H
#define __ASM_ARCH_TRACE_H

#include <pci.h>

#if IS_ENABLED(CONFIG_TURIN_REG_TRACE)
void turin_trace_smn(u32 addr, u32 val);
void turin_trace_pci(pci_dev_t bdf, uint offset, ulong val, int bits);
void turin_trace_msr(u32 msr, u64 val);
void turin_trace_mmio(ulong addr, ulong val, int bits);
void turin_trace_msg(char kind, u32 msg, const u32 *args, int count);
void turin_trace_resp(char kind, u32 resp, const u32 *args, int count);
#else
static inline void turin_trace_smn(u32 addr, u32 val) {}
static inline void turin_trace_pci(pci_dev_t bdf, uint offset, ulong val,
				   int bits) {}
static inline void turin_trace_msr(u32 msr, u64 val) {}
static inline void turin_trace_mmio(ulong addr, ulong val, int bits) {}
static inline void turin_trace_msg(char kind, u32 msg, const u32 *args,
				   int count) {}
static inline void turin_trace_resp(char kind, u32 resp, const u32 *args,
				    int count) {}
#endif

#endif
