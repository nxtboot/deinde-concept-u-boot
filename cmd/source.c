// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2001
 * Kyle Harris, kharris@nexus-tech.net
 */

/*
 * The "source" command allows to define "script images", i. e. files
 * that contain command sequences that can be executed by the command
 * interpreter. It returns the exit status of the last command
 * executed from the script. This is very similar to running a shell
 * script in a UNIX shell, hence the name for the command.
 */

/* #define DEBUG */

#include <command.h>
#include <env.h>
#include <dm.h>
#include <file.h>
#include <fs_common.h>
#include <image.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <vfs.h>
#include <asm/byteorder.h>
#include <asm/io.h>

#if IS_ENABLED(CONFIG_VFS)
/**
 * source_vfs_file() - Load and execute a script from a VFS path
 *
 * Loads the file into memory. If it has a valid image header, runs it
 * via cmd_source_script(). Otherwise treats it as a plain text command
 * list.
 *
 * @path: VFS path to the script file
 * Return: 0 on success, non-zero on error
 */
static int source_vfs_file(const char *path)
{
	struct file_uc_priv *uc_priv;
	struct udevice *fil;
	long actual;
	char *buf;
	ulong addr;
	int ret;

	ret = vfs_open_file(path, DIR_O_RDONLY, &fil);
	if (ret) {
		printf("## Cannot find script '%s'\n", path);
		return CMD_RET_FAILURE;
	}

	uc_priv = dev_get_uclass_priv(fil);
	buf = malloc(uc_priv->size + 1);
	if (!buf)
		return CMD_RET_FAILURE;

	actual = file_read_at(fil, buf, 0, uc_priv->size);
	if (actual < 0) {
		free(buf);
		printf("## Cannot read script '%s'\n", path);
		return CMD_RET_FAILURE;
	}

	buf[actual] = '\0';

	/* Check for a mkimage header (legacy or FIT) */
	if (actual >= sizeof(struct legacy_img_hdr) &&
	    image_get_magic((struct legacy_img_hdr *)buf) == IH_MAGIC) {
		addr = map_to_sysmem(buf);
		ret = cmd_source_script(addr, NULL, NULL);
	} else {
		/* Plain text command list */
		ret = run_command_list(buf, actual, 0);
	}

	free(buf);

	return ret;
}

static bool is_vfs_path(const char *arg)
{
	return arg && (arg[0] == '/' || strchr(arg, '/'));
}
#endif

static int do_source(struct cmd_tbl *cmdtp, int flag, int argc,
		     char *const argv[])
{
	ulong addr;
	int rcode;
	const char *fit_uname = NULL, *confname = NULL;

#if IS_ENABLED(CONFIG_VFS)
	if (argc >= 2 && is_vfs_path(argv[1])) {
		printf("## Executing script from '%s'\n", argv[1]);
		return source_vfs_file(argv[1]);
	}
#endif

	/* Find script image */
	if (argc < 2) {
		addr = CONFIG_SYS_LOAD_ADDR;
		debug("*  source: default load address = 0x%08lx\n", addr);
#if defined(CONFIG_FIT)
	} else if (fit_parse_subimage(argv[1], image_load_addr, &addr,
				      &fit_uname)) {
		debug("*  source: subimage '%s' from FIT image at 0x%08lx\n",
		      fit_uname, addr);
	} else if (fit_parse_conf(argv[1], image_load_addr, &addr, &confname)) {
		debug("*  source: config '%s' from FIT image at 0x%08lx\n",
		      confname, addr);
#endif
	} else {
		addr = hextoul(argv[1], NULL);
		debug("*  source: cmdline image address = 0x%08lx\n", addr);
	}

	printf ("## Executing script at %08lx\n", addr);
	rcode = cmd_source_script(addr, fit_uname, confname);
	return rcode;
}

U_BOOT_LONGHELP(source,
#if defined(CONFIG_FIT)
	"[<addr>][:[<image>]|#[<config>]]\n"
	"\t- Run script starting at addr\n"
	"\t- A FIT config name or subimage name may be specified with : or #\n"
	"\t  (like bootm). If the image or config name is omitted, the\n"
	"\t  default is used."
#else
	"[<addr>]\n"
	"\t- Run script starting at addr"
#endif
	);

U_BOOT_CMD(
	source, 2, 0,	do_source,
	"run script from memory", source_help_text
);
