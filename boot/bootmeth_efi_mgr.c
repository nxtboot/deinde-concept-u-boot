// SPDX-License-Identifier: GPL-2.0+
/*
 * Bootmethod for EFI boot manager
 *
 * Copyright 2021 Google LLC
 * Written by Simon Glass <sjg@chromium.org>
 */

#define LOG_CATEGORY UCLASS_BOOTSTD

#include <bootdev.h>
#include <bootflow.h>
#include <bootmeth.h>
#include <charset.h>
#include <command.h>
#include <dm.h>
#include <efi_loader.h>
#include <efi_variable.h>
#include <malloc.h>

/**
 * struct efi_mgr_priv - private info for the efi-mgr driver
 *
 * @fake_bootflow: Fake a valid bootflow for testing
 */
struct efi_mgr_priv {
	bool fake_dev;
};

void sandbox_set_fake_efi_mgr_dev(struct udevice *dev, bool fake_dev)
{
	struct efi_mgr_priv *priv = dev_get_priv(dev);

	priv->fake_dev = fake_dev;
}

static int efi_mgr_check(struct udevice *dev, struct bootflow_iter *iter)
{
	int ret;

	/* Must be an bootstd device */
	ret = bootflow_iter_check_system(iter);
	if (ret)
		return log_msg_ret("net", ret);

	return 0;
}

/**
 * efi_mgr_set_name() - Name the bootflow after the boot option it will run
 *
 * The boot manager runs BootNext if that is set, else the entries of
 * BootOrder in turn, so use the label of the first of those, e.g. 'virtio 0'
 * or 'ubuntu'. If the option cannot be read the bootflow keeps whatever name
 * the caller gives it.
 *
 * @bflow: Bootflow to name
 * @bootorder: Value of the BootOrder variable
 * @size: Size of @bootorder in bytes
 * Return: 0 if OK, -ENOMEM if out of memory, -ENOENT if there is no option
 */
static int efi_mgr_set_name(struct bootflow *bflow, const u16 *bootorder,
			    efi_uintn_t size)
{
	struct efi_load_option lo;
	u16 varname[9];
	efi_uintn_t lo_size;
	u16 *bootnext;
	void *data;
	char *name, *ptr;
	efi_status_t ret;
	u16 num;

	bootnext = efi_get_var(u"BootNext", &efi_global_variable_guid,
			       &lo_size);
	if (bootnext && lo_size == sizeof(*bootnext)) {
		num = *bootnext;
	} else if (size >= sizeof(*bootorder)) {
		num = bootorder[0];
	} else {
		free(bootnext);
		return -ENOENT;
	}
	free(bootnext);

	efi_create_indexed_name(varname, sizeof(varname), "Boot", num);
	data = efi_get_var(varname, &efi_global_variable_guid, &lo_size);
	if (!data)
		return -ENOENT;
	ret = efi_deserialize_load_option(&lo, data, &lo_size);
	if (ret != EFI_SUCCESS) {
		free(data);
		return -ENOENT;
	}

	name = malloc(utf16_utf8_strlen(lo.label) + 1);
	if (!name) {
		free(data);
		return -ENOMEM;
	}
	ptr = name;
	utf16_utf8_strcpy(&ptr, lo.label);
	*ptr = '\0';
	free(data);

	free(bflow->name);
	bflow->name = name;

	return 0;
}

static int efi_mgr_read_bootflow(struct udevice *dev, struct bootflow *bflow)
{
	struct efi_mgr_priv *priv = dev_get_priv(dev);
	efi_status_t ret;
	efi_uintn_t size;
	u16 *bootorder;

	if (priv->fake_dev) {
		bflow->state = BOOTFLOWST_READY;
		return 0;
	}

	ret = efi_init_obj_list();
	if (ret != EFI_SUCCESS)
		return log_msg_ret("init", -EIO);

	/* Enable this method if the "BootOrder" UEFI exists. */
	bootorder = efi_get_var(u"BootOrder", &efi_global_variable_guid,
				&size);
	if (bootorder) {
		int err;

		err = efi_mgr_set_name(bflow, bootorder, size);
		free(bootorder);
		if (err == -ENOMEM)
			return log_msg_ret("name", err);
		bflow->state = BOOTFLOWST_READY;
		return 0;
	}

	return -EINVAL;
}

static int efi_mgr_read_file(struct udevice *dev, struct bootflow *bflow,
			     const char *file_path, ulong *addrp, ulong align,
			     enum bootflow_img_t type, ulong *sizep)
{
	/* Files are loaded by the 'bootefi bootmgr' command */

	return -ENOSYS;
}

static int efi_mgr_boot(struct udevice *dev, struct bootflow *bflow)
{
	efi_status_t ret;

	/* Booting is handled by the 'bootefi bootmgr' command */
	ret = efi_bootmgr_run(EFI_FDT_USE_INTERNAL);
	if (ret != EFI_SUCCESS)
		return log_msg_ret("run", -EIO);

	return 0;
}

static int bootmeth_efi_mgr_bind(struct udevice *dev)
{
	struct bootmeth_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->desc = "EFI bootmgr flow";
	plat->flags = BOOTMETHF_GLOBAL;

	/*
	 * bootmgr scans all available devices which can take a while,
	 * especially for network devices. So choose the priority so that it
	 * comes just before the 'very slow' devices. This allows systems which
	 * don't rely on bootmgr to boot quickly, while allowing bootmgr to run
	 * on systems which need it.
	 */
	plat->glob_prio = BOOTDEVP_6_NET_BASE;

	return 0;
}

static struct bootmeth_ops efi_mgr_bootmeth_ops = {
	.check		= efi_mgr_check,
	.read_bootflow	= efi_mgr_read_bootflow,
	.read_file	= efi_mgr_read_file,
	.boot		= efi_mgr_boot,
};

static const struct udevice_id efi_mgr_bootmeth_ids[] = {
	{ .compatible = "u-boot,efi-bootmgr" },
	{ }
};

U_BOOT_DRIVER(bootmeth_3efi_mgr) = {
	.name		= "bootmeth_efi_mgr",
	.id		= UCLASS_BOOTMETH,
	.of_match	= efi_mgr_bootmeth_ids,
	.ops		= &efi_mgr_bootmeth_ops,
	.bind		= bootmeth_efi_mgr_bind,
	.priv_auto	= sizeof(struct efi_mgr_priv),
};
