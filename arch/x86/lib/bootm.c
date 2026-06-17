// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2002
 * Sysgo Real-Time Solutions, GmbH <www.elinos.com>
 * Marius Groeger <mgroeger@sysgo.de>
 *
 * Copyright (C) 2001  Erik Mouw (J.A.K.Mouw@its.tudelft.nl)
 */

#include <bootm.h>
#include <bootstage.h>
#include <command.h>
#include <efi.h>
#include <efi_api.h>
#include <hang.h>
#include <log.h>
#include <asm/global_data.h>
#include <dm/device.h>
#include <dm/root.h>
#include <errno.h>
#include <fdt_support.h>
#include <image.h>
#include <u-boot/zlib.h>
#include <asm/bootparam.h>
#include <asm/cpu.h>
#include <asm/byteorder.h>
#include <asm/post.h>
#include <asm/zimage.h>
#ifdef CONFIG_SYS_COREBOOT
#include <asm/arch/timestamp.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

#define COMMAND_LINE_OFFSET 0x9000

#if defined(CONFIG_OF_LIBFDT) && !defined(CONFIG_OF_NO_KERNEL)
int arch_fixup_memory_node(void *blob)
{
	int bank;
	u64 start[CONFIG_NR_DRAM_BANKS];
	u64 size[CONFIG_NR_DRAM_BANKS];

	for (bank = 0; bank < CONFIG_NR_DRAM_BANKS; bank++) {
		start[bank] = gd->dram[bank].start;
		size[bank] = gd->dram[bank].size;
	}

	return fdt_fixup_memory_banks(blob, start, size, CONFIG_NR_DRAM_BANKS);
}
#endif

/* Subcommand: PREP */
static int boot_prep_linux(struct bootm_headers *images)
{
	ulong initrd_start, initrd_size;
	char *cmd_line_dest = NULL;
	struct legacy_img_hdr *hdr;
	int is_zimage = 0;
	void *data = NULL;
	size_t len;
	int ret;

	if (CONFIG_IS_ENABLED(OF_LIBFDT) && IS_ENABLED(CONFIG_LMB) && images->ft_len) {
		debug("using: FDT\n");
		if (image_setup_linux(images)) {
			puts("FDT creation failed! hanging...");
			hang();
		}
	}

	if (images->legacy_hdr_valid) {
		hdr = images->legacy_hdr_os;
		if (image_check_type(hdr, IH_TYPE_MULTI)) {
			ulong os_data, os_len;

			/* if multi-part image, we need to get first subimage */
			image_multi_getimg(hdr, 0, &os_data, &os_len);
			data = (void *)os_data;
			len = os_len;
		} else {
			/* otherwise get image data */
			data = (void *)image_get_data(hdr);
			len = image_get_data_size(hdr);
		}
		is_zimage = 1;
#if defined(CONFIG_FIT)
	} else if (images->fit_uname_os) {
		ret = fit_image_get_data(images->fit_hdr_os,
					 images->fit_noffset_os,
					 (const void **)&data, &len);
		if (ret) {
			puts("Can't get image data/size!\n");
			goto error;
		}
		is_zimage = 1;
#endif
	}

	if (is_zimage) {
		ulong load_address;
		char *base_ptr;

		base_ptr = (char *)load_zimage(data, len, &load_address);
		if (!base_ptr) {
			puts("## Kernel loading failed ...\n");
			goto error;
		}
		images->os.load = load_address;
		cmd_line_dest = base_ptr + COMMAND_LINE_OFFSET;
		images->ep = (ulong)base_ptr;
	} else if (images->ep) {
		cmd_line_dest = (void *)images->ep + COMMAND_LINE_OFFSET;
	} else {
		printf("## Kernel loading failed (missing x86 kernel setup) ...\n");
		goto error;
	}

	if (IS_ENABLED(CONFIG_SYS_BOOT_RAMDISK_HIGH)) {
		initrd_start = images->initrd_start;
		initrd_size = images->initrd_end - images->initrd_start;
	} else {
		initrd_start = images->rd_start;
		initrd_size = images->rd_end - images->rd_start;
	}
	printf("Setup at %#08lx\n", images->ep);
	ret = setup_zimage((void *)images->ep, cmd_line_dest,
			   0, initrd_start, initrd_size, 0);

	if (ret) {
		printf("## Setting up boot parameters failed ...\n");
		return 1;
	}

	return 0;

error:
	return 1;
}

typedef void(*handover_func)(void *, struct efi_system_table *sys_table, struct
boot_params *params);

int efi_boot(ulong setup_base, ulong entry, bool image_64bit)
{
	struct boot_params *params = (struct boot_params *)setup_base;
	struct setup_header *hdr = &params->hdr;
	struct efi_priv *priv = efi_get_priv();
	handover_func hf;
	int offset = 0;

	if (IS_ENABLED(CONFIG_EFI_APP_64BIT)) {
		if (!image_64bit) {
			printf("## Can only boot 64-bit kernels\n");
			return 1;
		}
		offset = 512;
	} else if (image_64bit) {
		printf("# Can only boot 32-bit kernels\n");
		return 1;
	}

	hdr->code32_start = (int)entry;
	hdr->type_of_loader = 0x80; /* U-Boot, from Linux Documentation/x86/boot.rst */

	hf = (handover_func)(entry + hdr->handover_offset + offset);
	asm volatile ("cli");
	priv->loaded_image->image_base = (char *)entry;
	hf(priv->parent_image, priv->sys_table, params);
	return -EFAULT;
}

int boot_linux_kernel(ulong setup_base, ulong entry, bool image_64bit)
{
#ifdef CONFIG_SYS_COREBOOT
	timestamp_add_now(TS_START_KERNEL);
#endif
	bootm_final(0);

#ifdef CONFIG_SYS_COREBOOT
	timestamp_add_now(TS_U_BOOT_START_KERNEL);
#endif

	if (IS_ENABLED(CONFIG_EFI_APP))
		return efi_boot(setup_base, entry, image_64bit);

	post_code(POST_BOOT);

	if (image_64bit) {
		if (!cpu_has_64bit()) {
			puts("Cannot boot 64-bit kernel on 32-bit machine\n");
			return -EFAULT;
		}
		/*
		 * At present 64-bit U-Boot only supports booting a 64-bit
		 * kernel.
		 *
		 * TODO(sjg@chromium.org): Support booting 32-bit kernels from
		 * 64-bit U-Boot
		 */
		if (CONFIG_IS_ENABLED(X86_64)) {
			typedef void (*h_func)(ulong zero, ulong setup);
			h_func func;
			struct setup_header *hdr = &(((struct boot_params *)(setup_base))->hdr);

			/* Handle kernel with legacy 64-bit entry point at 0x200 */
			if (hdr->xloadflags & XLF_KERNEL_64) {
				entry += 0x200;
			}

			/* jump to Linux with rdi=0, rsi=setup_base */
			func = (h_func)entry;
			func(0, setup_base);
		} else {
			return cpu_jump_to_64bit(setup_base, entry);
		}
	} else {
		/*
		* Set %ebx, %ebp, and %edi to 0, %esi to point to the
		* boot_params structure, and then jump to the kernel. We
		* assume that %cs is 0x10, 4GB flat, and read/execute, and
		* the data segments are 0x18, 4GB flat, and read/write.
		* U-Boot is setting them up that way for itself in
		* arch/i386/cpu/cpu.c.
		*
		* Note: this is incomplete for EFI kernels!
		*
		* This can boot a kernel while running as an EFI application,
		* but if the kernel requires EFI support then that support needs
		* to be enabled first (see EFI_LOADER). Also the EFI information
		* must enabled with setup_efi_info(). See setup_zimage() for
		* how this is done with the stub.
		*/
		__asm__ __volatile__ (
		"movl $0, %%ebp\n"
		"cli\n"
		"jmp *%[kernel_entry]\n"
		:: [kernel_entry]"a"(entry),
		[boot_params] "S"(setup_base),
		"b"(0), "D"(0)
		);
	}

	/* We can't get to here */
	return -EFAULT;
}

/* Subcommand: GO */
static int boot_jump_linux(struct bootm_headers *images)
{
	debug("## Transferring control to Linux (at address %08lx, kernel %08lx) ...\n",
	      images->ep, images->os.load);

	return boot_linux_kernel(images->ep, images->os.load,
				 images->os.arch == IH_ARCH_X86_64);
}

int do_bootm_linux(int flag, struct bootm_info *bmi)
{
	struct bootm_headers *images = bmi->images;

	/* No need for those on x86 */
	if (flag & BOOTM_STATE_OS_BD_T || flag & BOOTM_STATE_OS_CMDLINE)
		return -1;

	if (flag & BOOTM_STATE_OS_PREP)
		return boot_prep_linux(images);

	if (flag & BOOTM_STATE_OS_GO)
		return boot_jump_linux(images);

	return boot_jump_linux(images);
}

int arch_upl_jump(ulong entry, const struct abuf *buf)
{
	typedef EFIAPI void (*h_func)(void *hoff);
	h_func func;

	func = (h_func)(ulong)entry;
	func(buf->data);

	return -EFAULT;
}
