// SPDX-License-Identifier: GPL-2.0+
/*
 *  For the code moved from cmd/bootefi.c
 *  Copyright (c) 2016 Alexander Graf
 */

#define LOG_CATEGORY LOGC_EFI

#include <bootflow.h>
#include <charset.h>
#include <dm.h>
#include <efi.h>
#include <efi_device_path.h>
#include <efi_loader.h>
#include <env.h>
#include <image.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <net.h>

/**
 * efi_get_image_parameters() - return image parameters
 *
 * @img_addr:		address of loaded image in memory
 * @img_size:		size of loaded image
 */
void efi_get_image_parameters(void **img_addr, size_t *img_size)
{
	const struct efi_bootefi *boot = &efis->bootefi;
	*img_addr = boot->image_addr;
	*img_size = boot->image_size;
}

/**
 * efi_clear_bootdev() - clear boot device
 */
void efi_clear_bootdev(void)
{
	struct efi_bootefi *boot = &efis->bootefi;

	efi_free_pool(boot->device_path);
	efi_free_pool(boot->image_path);
	boot->device_path = NULL;
	boot->image_path = NULL;
	boot->image_addr = NULL;
	boot->image_size = 0;
}

/**
 * efi_set_bootdev() - set boot device
 *
 * This function is called when a file is loaded, e.g. via the 'load' command.
 * We use the path to this file to inform the UEFI binary about the boot device.
 *
 * For a valid image, it sets these members of struct efi_bootefi in the state:
 *    - image_addr to the provided buffer
 *    - image_size to the provided buffer_size
 *    - device_path to the EFI device-path
 *    - image_path to the EFI image-path
 *
 * @dev:		device, e.g. "MMC"
 * @devnr:		number of the device, e.g. "1:2"
 * @path:		path to file loaded
 * @buffer:		buffer with file loaded
 * @buffer_size:	size of file loaded
 */
void efi_set_bootdev(const char *dev, const char *devnr, const char *path,
		     void *buffer, size_t buffer_size)
{
	struct efi_bootefi *boot = &efis->bootefi;
	efi_status_t ret;

	log_debug("dev=%s, devnr=%s, path=%s, buffer=%p, size=%zx\n", dev,
		  devnr, path, buffer, buffer_size);

	/* Forget overwritten image */
	if (buffer + buffer_size >= boot->image_addr &&
	    boot->image_addr + boot->image_size >= buffer)
		efi_clear_bootdev();

	/* Remember only PE-COFF and FIT images */
	if (efi_check_pe(buffer, buffer_size, NULL) != EFI_SUCCESS) {
		if (IS_ENABLED(CONFIG_FIT) &&
		    !fit_check_format(buffer, IMAGE_SIZE_INVAL)) {
			/*
			 * FIT images of type EFI_OS are started via command
			 * bootm. We should not use their boot device with the
			 * bootefi command.
			 */
			buffer = 0;
			buffer_size = 0;
		} else {
			log_debug("- not remembering image\n");
			return;
		}
	}

	/* efi_set_bootdev() is typically called repeatedly, recover memory */
	efi_clear_bootdev();

	boot->image_addr = buffer;
	boot->image_size = buffer_size;

	ret = calculate_paths(dev, devnr, path, &boot->device_path,
			      &boot->image_path);
	if (ret) {
		log_debug("- efi_dp_from_name() failed, err=%lx\n", ret);
		efi_clear_bootdev();
	}
}

/**
 * efi_binary_run() - run loaded UEFI image
 *
 * @image:	memory address of the UEFI image
 * @size:	size of the UEFI image
 * @fdt:	device-tree
 * @initrd:	initrd
 * @initrd_sz:	initrd size
 *
 * Execute an EFI binary image loaded at @image.
 * @size may be zero if the binary is loaded with U-Boot load command.
 *
 * Return:	status code
 */
efi_status_t efi_binary_run(void *image, size_t size, void *fdt, void *initrd, size_t initrd_sz)
{
	struct efi_bootefi *boot = &efis->bootefi;
	efi_handle_t mem_handle = NULL;
	struct efi_device_path *file_path = NULL;
	efi_status_t ret;

	if (!boot->device_path || !boot->image_path) {
		log_debug("Not loaded from disk\n");
		/*
		 * Special case for efi payload not loaded from disk,
		 * such as 'bootefi hello' or for example payload
		 * loaded directly into memory via JTAG, etc:
		 */
		file_path = efi_dp_from_mem(EFI_RESERVED_MEMORY_TYPE, image,
					    size);
		/*
		 * Make sure that device for device_path exist
		 * in load_image(). Otherwise, shell and grub will fail.
		 */
		ret = efi_install_multiple_protocol_interfaces(&mem_handle,
							       &efi_guid_device_path,
							       file_path, NULL);
		if (ret != EFI_SUCCESS)
			goto out;

		boot->device_path = file_path;
		boot->image_path = NULL;
	} else {
		log_debug("Loaded from disk\n");
	}

	ret = efi_binary_run_dp(image, size, fdt, initrd, initrd_sz,
				boot->device_path, boot->image_path);
out:
	if (mem_handle) {
		efi_status_t r;

		r = efi_uninstall_multiple_protocol_interfaces(mem_handle,
					&efi_guid_device_path, file_path, NULL);
		if (r != EFI_SUCCESS)
			log_err("Uninstalling protocol interfaces failed\n");
	}
	efi_free_pool(file_path);

	return ret;
}
