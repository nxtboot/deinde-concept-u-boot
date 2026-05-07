// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2024 Linaro Limited
 * Copyright 2024 Canonical Ltd
 */

#include <efi.h>
#include <efi_loader.h>

efi_status_t efi_run_image(void *source_buffer, efi_uintn_t source_size,
			   struct efi_device_path *dp_dev,
			   struct efi_device_path *dp_img)
{
	efi_handle_t handle;
	struct efi_device_path *msg_path, *file_path;
	efi_status_t ret;
	u16 *load_options;

	file_path = efi_dp_concat(dp_dev, dp_img, 0);
	msg_path = dp_img;

	log_info("Booting %pD\n", msg_path);

	ret = EFI_CALL(efi_load_image(false, efi_root, file_path, source_buffer,
				      source_size, &handle));
	if (ret != EFI_SUCCESS) {
		log_err("Loading image failed\n");
		goto out;
	}

	/* Transfer environment variable as load options */
	ret = efi_env_set_load_options(handle, "bootargs", &load_options);
	if (ret != EFI_SUCCESS)
		goto out;

	ret = do_bootefi_exec(handle, load_options);

out:

	return ret;
}

efi_status_t efi_binary_run_dp(void *image, size_t size, void *fdt,
			       void *initrd, size_t initrd_sz,
			       struct efi_device_path *dp_dev,
			       struct efi_device_path *dp_img)
{
	efi_status_t ret;

	/* Initialize EFI drivers */
	ret = efi_init_obj_list();
	if (ret != EFI_SUCCESS)
		return ret;

	ret = efi_install_fdt(fdt);
	if (ret != EFI_SUCCESS)
		return ret;

	ret = efi_install_initrd(initrd, initrd_sz);
	if (ret != EFI_SUCCESS)
		return ret;

	return efi_run_image(image, size, dp_dev, dp_img);
}
