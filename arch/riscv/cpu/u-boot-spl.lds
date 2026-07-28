/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Based on arch/riscv/cpu/u-boot.lds, which is
 * Copyright (C) 2017 Andes Technology Corporation
 * Rick Chen, Andes Technology Corporation <rick@andestech.com>
 *
 * and arch/mips/cpu/u-boot-spl.lds.
 */
MEMORY { .spl_mem : ORIGIN = IMAGE_TEXT_BASE, LENGTH = IMAGE_MAX_SIZE }
MEMORY { .bss_mem : ORIGIN = CONFIG_SPL_BSS_START_ADDR, \
		    LENGTH = CONFIG_SPL_BSS_MAX_SIZE }

OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
	. = ALIGN(4);
	__image_copy_start = ADDR(.text);
	.text : {
		arch/riscv/cpu/start.o	(.text)
		*(.text*)
	} > .spl_mem

	. = ALIGN(4);
	.rodata : {
		*(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rodata*)))
	} > .spl_mem

	. = ALIGN(4);
	.data : {
		*(.data*)
	} > .spl_mem
	. = ALIGN(4);

	__u_boot_list : {
		KEEP(*(SORT(__u_boot_list*)));
	} > .spl_mem

	. = ALIGN(4);

	/*
	 * libfdt requires any FDT it inspects to sit at an 8-byte aligned
	 * address (-FDT_ERR_ALIGNMENT since Linux 6.1 libfdt sync).  The DTB
	 * is concatenated to u-boot-spl-nodtb.bin right at its end, so pad
	 * inside .binman_sym_table so the raw binary length (and the
	 * _image_binary_end symbol) are both 8-byte aligned.
	 */
	.binman_sym_table : {
		__binman_sym_start = .;
		KEEP(*(SORT(.binman_sym*)));
		__binman_sym_end = .;
		. = ALIGN(8);
	} > .spl_mem

	_end = .;
	_image_binary_end = .;
	__image_copy_end = .;

	.bss : {
		__bss_start = .;
		*(.bss*)
		. = ALIGN(8);
		__bss_end = .;
	} > .bss_mem
}

ASSERT(_image_binary_end % 8 == 0, \
       "_image_binary_end must be 8-byte aligned for device tree");
