/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_TURIN_CPU_H
#define _ASM_ARCH_TURIN_CPU_H

/**
 * turin_ecam_init() - Move the PCIe ECAM window to where U-Boot can reach it
 *
 * Does nothing if it has already been moved
 */
void turin_ecam_init(void);

/**
 * turin_mpio_init() - Train the PCIe and SATA links described in /mpio
 *
 * Return: 0 if OK (or there is nothing to do), -ve on error
 */
int turin_mpio_init(void);

/**
 * turin_get_ioapic() - Find the I/O APIC of a root complex
 *
 * @busno: Root bus number
 * @addrp: Returns its MMIO address
 * @idp: Returns its ID
 * Return: 0 if OK, -ENOENT if it is not enabled, -ENODEV if the bus is not
 * a root bus
 */
int turin_get_ioapic(int busno, u32 *addrp, uint *idp);

#endif
