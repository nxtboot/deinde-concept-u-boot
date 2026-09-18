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
 * efi_mgr_option_label() - Get the label of a boot option, if it is usable
 *
 * @num: Boot option number, e.g. 1 for Boot0001
 * Return: Label as a new UTF-8 string which the caller must free, NULL if the
 * option does not exist, is not active or cannot be parsed, or on allocation
 * failure
 */
static char *efi_mgr_option_label(u16 num)
{
	struct efi_load_option lo;
	u16 varname[9];
	efi_uintn_t size;
	char *name, *ptr;
	efi_status_t ret;
	void *data;

	efi_create_indexed_name(varname, sizeof(varname), "Boot", num);
	data = efi_get_var(varname, &efi_global_variable_guid, &size);
	if (!data)
		return NULL;
	ret = efi_deserialize_load_option(&lo, data, &size);
	if (ret != EFI_SUCCESS || !(lo.attributes & LOAD_OPTION_ACTIVE)) {
		free(data);
		return NULL;
	}

	name = malloc(utf16_utf8_strlen(lo.label) + 1);
	if (name) {
		ptr = name;
		utf16_utf8_strcpy(&ptr, lo.label);
		*ptr = '\0';
	}
	free(data);

	return name;
}

/**
 * efi_mgr_set_name() - Name the bootflow after the boot option it will run
 *
 * The boot manager runs BootNext if that is set, else the entries of
 * BootOrder in turn, so use the label of the first of those which is active
 * and can be parsed, e.g. 'virtio 0' or 'Windows Boot Manager'. That is also
 * the check that there is something for the boot manager to run.
 *
 * @bflow: Bootflow to name
 * @bootorder: Value of the BootOrder variable
 * @size: Size of @bootorder in bytes
 * Return: 0 if OK, -ENOENT if there is no usable boot option
 */
static int efi_mgr_set_name(struct bootflow *bflow, const u16 *bootorder,
			    efi_uintn_t size)
{
	char *name = NULL;
	efi_uintn_t next_size;
	u16 *bootnext;
	int i;

	bootnext = efi_get_var(u"BootNext", &efi_global_variable_guid,
			       &next_size);
	if (bootnext && next_size == sizeof(*bootnext))
		name = efi_mgr_option_label(*bootnext);
	free(bootnext);

	for (i = 0; !name && i < size / sizeof(*bootorder); i++)
		name = efi_mgr_option_label(bootorder[i]);
	if (!name)
		return -ENOENT;

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
	int err;

	if (priv->fake_dev) {
		bflow->state = BOOTFLOWST_READY;
		return 0;
	}

	ret = efi_init_obj_list();
	if (ret != EFI_SUCCESS)
		return log_msg_ret("init", -EIO);

	/* Enable this method if BootOrder or BootNext names a usable option */
	bootorder = efi_get_var(u"BootOrder", &efi_global_variable_guid,
				&size);
	if (!bootorder) {
		bootorder = NULL;
		size = 0;
	}
	err = efi_mgr_set_name(bflow, bootorder, size);
	free(bootorder);
	if (err)
		return log_msg_ret("opt", err);
	bflow->state = BOOTFLOWST_READY;

	return 0;
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
