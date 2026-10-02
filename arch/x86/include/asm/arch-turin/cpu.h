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

/**
 * turin_get_iommu() - Find the IOMMU of a root complex
 *
 * Only the big IOHCs have one; it covers the small IOHC paired with it too
 *
 * @busno: Root bus number
 * @basep: Returns the MMIO address of its registers
 * Return: 0 if OK, -ENOENT if there is none, -ENODEV if the bus is not a
 * root bus
 */
int turin_get_iommu(int busno, u32 *basep);

/**
 * turin_get_paired_bus() - Find the small IOHC paired with a big one
 *
 * @busno: Root bus number of the big IOHC
 * Return: root bus number of the small IOHC, or -ENOENT if there is none
 */
int turin_get_paired_bus(int busno);

/* Pins on the FCH's I/O APIC and on each root complex's, and the bus stride */
#define FCH_IOAPIC_PINS		24
#define NBIO_IOAPIC_PINS	32
#define TURIN_BUSES_PER_ROOT	0x20
#define PCI_BUS_COUNT		0x100
#define FCH_ROOT_BUS		0	/* the root complex with the FCH */

/**
 * turin_gsi_base() - Get the first GSI of a root complex's I/O APIC
 *
 * The FCH's I/O APIC has the first GSIs, then each root complex's follows in
 * bus order, as the MADT reports them
 *
 * @busno: Root bus number
 * Return: first global system interrupt of the bus's I/O APIC
 */
int turin_gsi_base(int busno);

/**
 * turin_start_aps() - Give each AP the boot processor's patch and memory map
 *
 * @ucode: Microcode patch to load, or NULL for none
 * Return: 0 if OK, -EIO if not every AP started, other -ve on error
 */
int turin_start_aps(const void *ucode);

/**
 * turin_find_aps() - Find the APs which openSIL has started
 *
 * openSIL starts the other threads itself, so this records their APIC IDs
 * for turin_get_cpus(), working them out from the threads which the SMU has
 * released as openSIL does
 *
 * Return: 0 if OK, -EIO if the count does not match CPUID's, other -ve on
 * error
 */
int turin_find_aps(void);

/**
 * turin_get_cpus() - Get the APIC IDs of the CPUs
 *
 * @idsp: Returns the APIC IDs, the boot processor's first
 * Return: number of CPUs
 */
int turin_get_cpus(const u8 **idsp);

/* Number of argument words in an SMU request, and its success code */
#define SMU_NUM_ARGS		6
#define SMU_RESULT_OK		1

/**
 * turin_smu_request() - Send a message to the SMU and wait for its reply
 *
 * @msg: Message ID
 * @args: SMU_NUM_ARGS argument words, updated with the SMU's reply
 * Return: SMU result code (SMU_RESULT_OK on success), or -ETIMEDOUT
 */
int turin_smu_request(u32 msg, u32 *args);

#endif
