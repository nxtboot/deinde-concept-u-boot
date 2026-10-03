// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018, Google Inc.
 * Written by Simon Glass <sjg@chromium.org>
 */

#include <command.h>
#include <dm.h>
#include <spl.h>
#include <asm/cpu.h>
#include <asm/global_data.h>
#include <asm/state.h>
#include <dm/device-internal.h>
#include <dm/lists.h>

DECLARE_GLOBAL_DATA_PTR;

static int do_sb_handoff(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
#if IS_ENABLED(CONFIG_HANDOFF)
	if (gd->spl_handoff)
		printf("SPL handoff magic %lx\n", gd->spl_handoff->arch.magic);
	else
		printf("SPL handoff info not received\n");

	return 0;
#else
	printf("Command not supported\n");

	return CMD_RET_USAGE;
#endif
}

static int do_sb_map(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	sandbox_map_list();

	return 0;
}

static int do_sb_state(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	struct sandbox_state *state;

	state = state_get_current();
	state_show(state);

	return 0;
}

static int do_sb_devon(struct cmd_tbl *cmdtp, int flag, int argc,
		       char *const argv[])
{
	struct udevice *dev;
	ofnode root, node;
	int ret;

	if (argc != 2)
		return CMD_RET_USAGE;

	/* Find the specified device tree node */
	root = oftree_root(oftree_default());
	node = ofnode_find_subnode(root, argv[1]);
	if (!ofnode_valid(node)) {
		printf("Device tree node '%s' not found\n", argv[1]);
		return CMD_RET_FAILURE;
	}

	/* Check if device is already bound */
	ret = device_find_global_by_ofnode(node, &dev);
	if (!ret) {
		printf("Device '%s' is already enabled\n", argv[1]);
		return CMD_RET_FAILURE;
	}

	/* Bind the device from device tree */
	ret = lists_bind_fdt(gd->dm_root, node, &dev, NULL, false);
	if (ret) {
		printf("Failed to bind device '%s' (err %dE)\n", argv[1],
		       ret);
		return CMD_RET_FAILURE;
	}

	/* Probe the device */
	ret = device_probe(dev);
	if (ret) {
		printf("Failed to probe device '%s' (err %dE)\n", argv[1],
		       ret);
		return CMD_RET_FAILURE;
	}

	printf("Device '%s' enabled\n", dev->name);

	return CMD_RET_SUCCESS;
}

static int do_sb_grid(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	struct sandbox_state *state = state_get_current();

	if (argc < 2)
		return CMD_RET_USAGE;

	state->show_grid = hextoul(argv[1], NULL);
	if (argc >= 3)
		state->grid_size = hextoul(argv[2], NULL);

	return 0;
}

static int do_sb_devoff(struct cmd_tbl *cmdtp, int flag, int argc,
			char *const argv[])
{
	struct udevice *dev;
	ofnode root, node;
	int ret;

	if (argc != 2)
		return CMD_RET_USAGE;

	/* Find the specified device tree node */
	root = oftree_root(oftree_default());
	node = ofnode_find_subnode(root, argv[1]);
	if (!ofnode_valid(node)) {
		printf("Device tree node '%s' not found\n", argv[1]);
		return CMD_RET_FAILURE;
	}

	/* Find the device bound to this node */
	ret = device_find_global_by_ofnode(node, &dev);
	if (ret) {
		printf("Device '%s' not found or not bound (err %dE)\n",
		       argv[1], ret);
		return CMD_RET_FAILURE;
	}

	/* Remove the device (deactivate it) */
	ret = device_remove(dev, DM_REMOVE_NORMAL);
	if (ret) {
		printf("Failed to remove device '%s' (err %dE)\n", argv[1],
		       ret);
		return CMD_RET_FAILURE;
	}

	/* Unbind the device */
	ret = device_unbind(dev);
	if (ret) {
		printf("Failed to unbind device '%s' (err %dE)\n", argv[1],
		       ret);
		return CMD_RET_FAILURE;
	}

	printf("Device '%s' disabled\n", argv[1]);

	return CMD_RET_SUCCESS;
}

U_BOOT_LONGHELP(sb,
	"devoff <node>  - Disable device from device tree node\n"
	"sb devon <node>   - Enable device from device tree node\n"
	"sb grid <0|1> [<size>] - Enable/disable grid overlay on video\n"
	"sb handoff        - Show handoff data received from SPL\n"
	"sb map            - Show mapped memory\n"
	"sb state          - Show sandbox state");

U_BOOT_CMD_WITH_SUBCMDS(sb, "Sandbox status commands", sb_help_text,
	U_BOOT_SUBCMD_MKENT(devoff, 2, 1, do_sb_devoff),
	U_BOOT_SUBCMD_MKENT(devon, 2, 1, do_sb_devon),
	U_BOOT_SUBCMD_MKENT(grid, 3, 1, do_sb_grid),
	U_BOOT_SUBCMD_MKENT(handoff, 1, 1, do_sb_handoff),
	U_BOOT_SUBCMD_MKENT(map, 1, 1, do_sb_map),
	U_BOOT_SUBCMD_MKENT(state, 1, 1, do_sb_state));
