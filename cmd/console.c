// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

/*
 * Boot support
 */
#include <command.h>
#include <dm.h>
#include <iomux.h>
#include <stdio_dev.h>

/* shenangans to avoid code-size increase */
#ifdef CONFIG_CMD_CONSOLE_EXTRA
#define USE_NL ""
#else
#define USE_NL "\n"
#endif

static int do_coninfo(struct cmd_tbl *cmd, int flag, int argc,
		      char *const argv[])
{
	int l;
	struct list_head *list = stdio_get_list();
	struct list_head *pos;
	struct stdio_dev *sdev;

	/* Scan for valid output and input devices */
	puts("List of available devices\n\n");
	if (IS_ENABLED(CONFIG_CMD_CONSOLE_EXTRA))
		puts("Device  File               Uclass\n");

	list_for_each(pos, list) {
		sdev = list_entry(pos, struct stdio_dev, list);

		printf("|-- %s (%s%s)" USE_NL,
		       sdev->name,
		       (sdev->flags & DEV_FLAGS_INPUT) ? "I" : "",
		       (sdev->flags & DEV_FLAGS_OUTPUT) ? "O" : "");

		if (IS_ENABLED(CONFIG_CMD_CONSOLE_EXTRA) &&
		    IS_ENABLED(CONFIG_DM_STDIO) &&
		    (sdev->flags & DEV_FLAGS_DM)) {
			struct udevice *dev = sdev->priv;
			int len = 0;

			len += (sdev->flags & DEV_FLAGS_INPUT) != 0;
			len += (sdev->flags & DEV_FLAGS_OUTPUT) != 0;
			printf("%*s%s", 20 - len - (int)strlen(sdev->name), "",
			       dev_get_uclass_name(dev));
		}
		if (IS_ENABLED(CONFIG_CMD_CONSOLE_EXTRA))
			puts("\n");

		for (l = 0; l < MAX_FILES; l++) {
			if (IS_ENABLED(CONFIG_CONSOLE_MUX)) {
				if (iomux_match_device(console_devices[l],
						       cd_count[l], sdev) >= 0)
					printf("|   |-- %s\n", stdio_names[l]);
			} else {
				if (stdio_devices[l] == sdev)
					printf("|   |-- %s\n", stdio_names[l]);
			}
		}
	}
	return 0;
}

/***************************************************/

U_BOOT_CMD(
	coninfo,	3,	1,	do_coninfo,
	"print console devices and information",
	""
);
