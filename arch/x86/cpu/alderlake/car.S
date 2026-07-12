/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2026 Simon Glass <sjg@chromium.org>
 *
 * Cache-as-RAM setup using FSP 2.x TempRamInit, which performs the
 * enhanced-NEM configuration for Alder Lake. This runs execute-in-place
 * from the SPI flash, before any stack exists, so calls are made with a
 * handcrafted stack in ROM, in the same way as the FSP1 version.
 */

#include <config.h>
#include <asm/post.h>

.globl car_init
car_init:
	/*
	 * Note: ebp holds the BIST value (built-in self test) so far, but
	 * ebp will be destroyed through the FSP call, thus we have to test
	 * the BIST value here before we call into FSP.
	 */
	test	%ebp, %ebp
	jz	car_init_start
	post_code(POST_BIST_FAILURE)
	jmp	die

car_init_start:
	post_code(POST_CAR_START)

	/*
	 * The FSP_INFO_HEADER cannot easily be parsed without a stack, so
	 * the TempRamInit entry point is taken from Kconfig; it must match
	 * the TempRamInitEntryOffset field of the FSP-T binary in use
	 */
	mov	$(CONFIG_FSP_T_ADDR + CONFIG_FSP_T_ENTRY_OFFSET), %eax

	/* call FSP TempRamInit to set up a temporary stack */
	lea	temp_ram_init_romstack, %esp
	jmp	*%eax

temp_ram_init_ret:
	addl	$4, %esp
	cmp	$0, %eax
	jnz	car_init_fail

	post_code(POST_CAR_CPU_CACHE)

	/*
	 * TempRamInit initialises ecx and edx to point to a temporary but
	 * writable memory range (Cache-as-RAM): ecx is the start of the
	 * range and edx the end. FSP decides where this lives, so use what
	 * it reports rather than SYS_CAR_ADDR/SYS_CAR_SIZE; this is why
	 * USE_CAR is not selected, so that start.S leaves esp alone.
	 */

	/* stack grows down from top of CAR */
	movl	%edx, %esp
	subl	$4, %esp

	xor	%esi, %esi
	xorl	%ebp, %ebp
	jmp	car_init_ret

car_init_fail:
	post_code(POST_CAR_FAILURE)

die:
	hlt
	jmp	die
	hlt

	/*
	 * Handcrafted ROM stack for the TempRamInit call: the return
	 * address, then the single parameter, a pointer to the FSPT_UPD
	 * region. NULL selects the defaults built into the FSP-T binary.
	 */
	.balign	4
temp_ram_init_romstack:
	.long	temp_ram_init_ret
	.long	0			/* FSPT_UPD pointer (NULL) */
