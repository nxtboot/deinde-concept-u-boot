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

#endif /* _ASM_ARCH_CPU_H */
