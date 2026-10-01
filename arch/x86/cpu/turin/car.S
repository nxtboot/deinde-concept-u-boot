/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 */

/*
 * The PSP has trained the memory before the CPU starts, so SPL's early stack
 * at CONFIG_SYS_CAR_ADDR is already in DRAM; there is no cache-as-RAM to set
 * up
 */
.globl car_init
car_init:
	jmp	car_init_ret
