// SPDX-License-Identifier: GPL-2.0+
/*
 * 'bootctl' command
 *
 * Copyright 2025 Canonical Ltd
 * Written by Simon Glass <simon.glass@canonical.com>
 */

#include <bootctl.h>
#include <command.h>
#include <dm.h>

static int do_bootctl_list(struct cmd_tbl *cmdtp, int flag, int argc,
			   char *const argv[])
{
	struct udevice *dev;
	struct uclass *uc;
	int i;

	printf("Seq  Name            Type            Description\n");
	printf("---  --------------  --------------  --------------------\n");

	i = 0;
	uclass_id_foreach_dev(UCLASS_BOOTCTL, dev, uc) {
		struct bootctl_uc_plat *ucp = dev_get_uclass_plat(dev);

		printf("%3d  %-15.15s %-15.15s %s\n", i, dev->name,
		       dev_get_uclass_name(dev), ucp->desc);
		i++;
	}

	printf("---  --------------  --------------  --------------------\n");
	printf("(%d driver%s)\n", i, i != 1 ? "s" : "");

	return 0;
}

static int do_bootctl_run(struct cmd_tbl *cmdtp, int flag, int argc,
			  char *const argv[])
{
	int ret;

	ret = bootctl_run();
	if (ret) {
		printf("Boot failed (err=%dE)\n", ret);
		return CMD_RET_FAILURE;
	}

	return 0;
}

U_BOOT_LONGHELP(bootctl,
	"list      - list bootctl drivers\n"
	"bootctl run       - run a boot");

U_BOOT_CMD_WITH_SUBCMDS(bootctl, "Boot control", bootctl_help_text,
	U_BOOT_SUBCMD_MKENT(list, 1, 1, do_bootctl_list),
	U_BOOT_SUBCMD_MKENT(run, 1, 1, do_bootctl_run));
