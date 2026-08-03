/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

#ifndef _ASM_ARCH_CPU_H
#define _ASM_ARCH_CPU_H

/* Crystal-clock frequency, used by the TSC timer */
#define CTC_FREQ		38400000

/**
 * adl_log_pm_state() - Log reset-cause and SMI/TCO status registers
 *
 * This reports the PMC's global-reset cause registers, the TCO watchdog
 * status and the SMI enable/status registers, to help diagnose a platform
 * reset or freeze on a previous boot
 *
 * @when: Tag to include in the log line, e.g. "pre-fspm"
 */
void adl_log_pm_state(const char *when);

/**
 * adl_find_microcode() - Point ucode_base at this CPU's microcode update
 *
 * The FIT gives the boot processor its microcode, but the application
 * processors start with none, which makes them unreliable, so mp_init
 * has each one load the update at ucode_base. Find the update matching
 * this CPU in the image's microcode collection, which may hold several
 * (e.g. for different steppings)
 *
 * This is also needed before silicon init, since the FSP reloads the
 * microcode from the region it is given. It does nothing if ucode_base
 * is already set
 */
void adl_find_microcode(void);

/**
 * adl_release_ssd_reset() - Release the NVMe SSD's PERST# signal
 *
 * The SSD is powered up with its reset held when the pads are first set
 * up; this releases the reset once the power is stable, so that silicon
 * init can train the PCIe link
 */
void adl_release_ssd_reset(void);

#endif /* _ASM_ARCH_CPU_H */
