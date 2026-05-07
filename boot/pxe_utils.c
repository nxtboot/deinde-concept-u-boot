// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 */

#define LOG_CATEGORY	LOGC_BOOT

#include <bootflow.h>
#include <bootm.h>
#include <command.h>
#include <dm.h>
#include <env.h>
#include <extension_board.h>
#include <image.h>
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <net.h>
#include <fdt_support.h>
#include <video.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <errno.h>
#include <linux/list.h>

#include <rng.h>

#include <splash.h>
#include <asm/io.h>

#include "menu.h"
#include "cli.h"

#include "pxe_utils.h"

#define MAX_TFTP_PATH_LEN 512

int pxe_get_file_size(ulong *sizep)
{
	const char *val;

	val = from_env("filesize");
	if (!val)
		return -ENOENT;

	if (strict_strtoul(val, 16, sizep) < 0)
		return -EINVAL;

	return 0;
}

/**
 * format_mac_pxe() - obtain a MAC address in the PXE format
 *
 * This produces a MAC-address string in the format for the current ethernet
 * device:
 *
 *   01-aa-bb-cc-dd-ee-ff
 *
 * where aa-ff is the MAC address in hex
 *
 * @outbuf: Buffer to write string to
 * @outbuf_len: length of buffer
 * Return: 1 if OK, -ENOSPC if buffer is too small, -ENOENT is there is no
 *	current ethernet device
 */
int format_mac_pxe(char *outbuf, size_t outbuf_len)
{
	uchar ethaddr[6];

	if (outbuf_len < 21) {
		printf("outbuf is too small (%zd < 21)\n", outbuf_len);
		return -ENOSPC;
	}

	if (!IS_ENABLED(CONFIG_NET))
		return -ENOENT;

	if (!eth_env_get_enetaddr_by_index("eth", eth_get_dev_index(), ethaddr))
		return -ENOENT;

	sprintf(outbuf, "01-%02x-%02x-%02x-%02x-%02x-%02x",
		ethaddr[0], ethaddr[1], ethaddr[2],
		ethaddr[3], ethaddr[4], ethaddr[5]);

	return 1;
}

/**
 * get_relfile() - read a file relative to the PXE file
 *
 * As in pxelinux, paths to files referenced from files we retrieve are
 * relative to the location of bootfile. get_relfile takes such a path and
 * joins it with the bootfile path to get the full path to the target file. If
 * the bootfile path is NULL, we use file_path as is.
 *
 * @ctx: PXE context
 * @file_path: File path to read (relative to the PXE file)
 * @addrp: On entry, address to load file or 0 to reserve an address with lmb;
 * on exit, address to which the file was loaded
 * @align: Reservation alignment, if using lmb
 * @filesizep: If not NULL, returns the file size in bytes
 * Returns 1 for success, or < 0 on error
 */
static int get_relfile(struct pxe_context *ctx, const char *file_path,
		       ulong *addrp, ulong align, enum bootflow_img_t type,
		       ulong *filesizep)
{
	size_t path_len;
	char relfile[MAX_TFTP_PATH_LEN + 1];
	ulong size;
	int ret;

	if (file_path[0] == '/' && ctx->allow_abs_path)
		*relfile = '\0';
	else
		strncpy(relfile, ctx->bootdir, MAX_TFTP_PATH_LEN);

	path_len = strlen(file_path) + strlen(relfile);

	if (path_len > MAX_TFTP_PATH_LEN) {
		printf("Base path too long (%s%s)\n", relfile, file_path);

		return -ENAMETOOLONG;
	}

	strcat(relfile, file_path);

	if (!ctx->quiet)
		printf("Retrieving file: %s\n", relfile);

	ret = ctx->getfile(ctx, relfile, addrp, align, type, &size);
	if (ret < 0)
		return log_msg_ret("get", ret);
	if (filesizep)
		*filesizep = size;

	return 1;
}

int get_pxe_file(struct pxe_context *ctx, const char *file_path,
		 ulong file_addr)
{
	ulong size;
	int err;
	char *buf;

	err = get_relfile(ctx, file_path, &file_addr, 0, BFI_EXTLINUX_CFG,
			  &size);
	if (err < 0)
		return err;

	buf = map_sysmem(file_addr + size, 1);
	*buf = '\0';
	unmap_sysmem(buf);
	ctx->pxe_file_size = size;

	return 1;
}

#define PXELINUX_DIR "pxelinux.cfg/"

/**
 * get_pxelinux_path() - Get a file in the pxelinux.cfg/ directory
 *
 * @ctx: PXE context
 * @file: Filename to process (relative to pxelinux.cfg/)
 * Returns 1 for success, -ENAMETOOLONG if the resulting path is too long.
 *	or other value < 0 on other error
 */
int get_pxelinux_path(struct pxe_context *ctx, const char *file,
		      unsigned long pxefile_addr_r)
{
	size_t base_len = strlen(PXELINUX_DIR);
	char path[MAX_TFTP_PATH_LEN + 1];

	if (base_len + strlen(file) > MAX_TFTP_PATH_LEN) {
		printf("path (%s%s) too long, skipping\n",
		       PXELINUX_DIR, file);
		return -ENAMETOOLONG;
	}

	sprintf(path, PXELINUX_DIR "%s", file);

	return get_pxe_file(ctx, path, pxefile_addr_r);
}

/**
 * get_relfile_envaddr() - read a file to an address in an env var
 *
 * Wrapper to make it easier to store the file at file_path in the location
 * specified by envaddr_name. file_path will be joined to the bootfile path,
 * if any is specified.
 *
 * @ctx: PXE context
 * @file_path: File path to read (relative to the PXE file)
 * @envaddr_name: Name of environment variable which contains the address to
 *	load to. If this doesn't exist, an address is reserved using LMB
 * @align: Reservation alignment, if using lmb
 * @type: File type
 * @addrp: Returns the address to which the file was loaded, on success
 * @filesizep: Returns the file size in bytes
 * Returns 1 on success, -ENOENT if @envaddr_name does not exist as an
 *	environment variable, -EINVAL if its format is not valid hex, or other
 *	value < 0 on other error
 */
static int get_relfile_envaddr(struct pxe_context *ctx, const char *file_path,
			       const char *envaddr_name, ulong align,
			       enum bootflow_img_t type, ulong *addrp,
			       ulong *filesizep)
{
	ulong addr = 0;
	char *envaddr;
	int ret;

	/*
	 * set the address if we have it, otherwise get_relfile() will reserve
	 * a space
	 */
	envaddr = env_get(envaddr_name);
	if (envaddr && strict_strtoul(envaddr, 16, &addr) < 0)
		return -EINVAL;

	ret = get_relfile(ctx, file_path, &addr, align, type, filesizep);
	if (ret != 1)
		return ret;
	*addrp = addr;

	return 1;
}

/**
 * label_print() - Print a label and its string members if they're defined
 *
 * This is passed as a callback to the menu code for displaying each
 * menu entry.
 *
 * @data: Label to print (is cast to struct pxe_label *)
 */
static void label_print(void *data)
{
	struct pxe_label *label = data;
	const char *c = label->menu ? label->menu : label->name;

	printf("%s:\t%s\n", label->num, c);
}

/**
 * label_localboot() - Boot a label that specified 'localboot'
 *
 * This requires that the 'localcmd' environment variable is defined. Its
 * contents will be executed as U-Boot commands.  If the label specified an
 * 'append' line, its contents will be used to overwrite the contents of the
 * 'bootargs' environment variable prior to running 'localcmd'.
 *
 * @label: Label to process
 * Returns 1 on success or < 0 on error
 */
static int label_localboot(struct pxe_label *label)
{
	char *localcmd;

	localcmd = from_env("localcmd");
	if (!localcmd)
		return -ENOENT;

	if (label->append) {
		char bootargs[CONFIG_SYS_CBSIZE];

		cli_simple_process_macros(label->append, bootargs,
					  sizeof(bootargs));
		env_set("bootargs", bootargs);
	}

	debug("running: %s\n", localcmd);

	return run_command_list(localcmd, strlen(localcmd), 0);
}

/*
 * label_boot_kaslrseed generate kaslrseed from hw rng
 */
static void label_boot_kaslrseed(struct pxe_context *ctx)
{
#if CONFIG_IS_ENABLED(DM_RNG)
	int err;

	err = fdt_check_header(ctx->fdt);
	if (err)
		return;

	/* add extra size for holding kaslr-seed */
	/* err is new fdt size, 0 or negtive */
	err = fdt_shrink_to_minimum(ctx->fdt, 512);
	if (err <= 0)
		return;

	fdt_kaslrseed(ctx->fdt, true);
#endif
}

/**
 * label_load_fdtoverlays() - Load FDT overlay files
 *
 * Load all overlay files specified in the label. The loaded addresses are
 * stored in each overlay's addr field.
 *
 * @ctx: PXE context
 * @label: Label to process
 */
static void label_load_fdtoverlays(struct pxe_context *ctx,
				   struct pxe_label *label)
{
	struct pxe_file *overlay;
	ulong fdtoverlay_addr;
	bool use_lmb;
	char *envaddr;

	/*
	 * Get the overlay load address. If fdtoverlay_addr_r is defined,
	 * overlays are loaded sequentially at increasing addresses. Otherwise,
	 * LMB allocates a fresh address for each overlay.
	 */
	envaddr = env_get("fdtoverlay_addr_r");
	if (envaddr) {
		fdtoverlay_addr = hextoul(envaddr, NULL);
		use_lmb = false;
	} else {
		fdtoverlay_addr = 0;
		use_lmb = true;
	}

	alist_for_each(overlay, &label->files) {
		ulong addr = fdtoverlay_addr;
		ulong size;
		int err;

		if (overlay->type != PFT_FDTOVERLAY)
			continue;

		err = get_relfile(ctx, overlay->path, &addr, SZ_4K,
				  (enum bootflow_img_t)IH_TYPE_FLATDT, &size);
		if (err < 0) {
			printf("Failed loading overlay %s\n", overlay->path);
			continue;
		}
		overlay->addr = addr;

		/*
		 * Move to next address if using fixed addresses.  libfdt
		 * requires the blob to be 8-byte aligned, so round up so the
		 * next overlay lands on a suitable boundary.
		 */
		if (!use_lmb)
			fdtoverlay_addr = ALIGN(addr + size, 8);
	}
}

/**
 * label_apply_fdtoverlays() - Apply loaded FDT overlays to working FDT
 *
 * Apply all previously loaded overlays to the working FDT.
 *
 * @ctx: PXE context
 * @label: Label containing overlays to apply
 */
static void label_apply_fdtoverlays(struct pxe_context *ctx,
				    struct pxe_label *label)
{
	struct pxe_file *overlay;
	struct fdt_header *blob;
	int err;

	err = fdt_check_header(ctx->fdt);
	if (err)
		return;

	/* Resize main fdt to make room for overlays */
	fdt_shrink_to_minimum(ctx->fdt, 8192);

	alist_for_each(overlay, &label->files) {
		if (overlay->type != PFT_FDTOVERLAY || !overlay->addr)
			continue;

		blob = map_sysmem(overlay->addr, 0);
		err = fdt_check_header(blob);
		if (err) {
			printf("Invalid overlay %s, skipping\n", overlay->path);
			continue;
		}

		err = fdt_overlay_apply_verbose(ctx->fdt, blob);
		if (err)
			printf("Failed to apply overlay %s, skipping\n",
			       overlay->path);
	}
}

/**
 * label_boot_extension() - Scan extension boards and load associated overlays
 *
 * Scan for available extension boards and load their devicetree overlays
 * during PXE boot.
 *
 * @ctx: PXE context
 * @label: Label containing boot configuration
 */
static void label_boot_extension(struct pxe_context *ctx,
				 struct pxe_label *label)
{
#if CONFIG_IS_ENABLED(SUPPORT_EXTENSION_SCAN)
	const struct extension *extension;
	struct fdt_header *working_fdt;
	struct alist *extension_list;
	int ret, dir_len, len = 0;
	char *overlay_dir;
	const char *slash;
	ulong fdt_addr;

	ret = extension_scan();
	if (ret < 0)
		return;

	extension_list = extension_get_list();
	if (!extension_list)
		return;

	/* Get the main fdt and map it */
	fdt_addr = env_get_hex("fdt_addr_r", 0);
	working_fdt = map_sysmem(fdt_addr, 0);
	if (fdt_check_header(working_fdt))
		return;

	/* Use fdtdir for now as the overlay devicetree directory */
	if (label->fdtdir) {
		len = strlen(label->fdtdir);
		if (!len)
			slash = "./";
		else if (label->fdtdir[len - 1] != '/')
			slash = "/";
		else
			slash = "";
	} else {
		slash = "/";
	}
	dir_len = len + strlen(slash) + 1;

	overlay_dir = calloc(1, dir_len);
	if (!overlay_dir)
		return;

	snprintf(overlay_dir, dir_len, "%s%s", label->fdtdir ?: "", slash);

	alist_for_each(extension, extension_list) {
		ulong overlay_addr, size;
		char *overlay_file;

		len = dir_len + strlen(extension->overlay);
		overlay_file = calloc(1, len);
		if (!overlay_file)
			goto cleanup;

		snprintf(overlay_file, len, "%s%s", overlay_dir,
			 extension->overlay);

		/* Load extension overlay file */
		ret = get_relfile_envaddr(ctx, overlay_file,
					  "extension_overlay_addr", 0,
					  (enum bootflow_img_t)IH_TYPE_FLATDT,
					  &overlay_addr, &size);
		if (ret < 0) {
			printf("Failed loading overlay %s\n", overlay_file);
			free(overlay_file);
			continue;
		}

		ret = extension_apply(working_fdt, size);
		if (ret) {
			printf("Failed applying overlay %s\n", overlay_file);
			free(overlay_file);
			continue;
		}
		free(overlay_file);
	}

cleanup:
	free(overlay_dir);
#endif
}

const char *pxe_get_fdt_fallback(struct pxe_label *label, ulong kern_addr)
{
	const char *conf_fdt_str = NULL;
	void *buf;

	/*
	 * Fallback to fdt_addr env var if label doesn't specify FDT
	 * and it's not ATAG mode (fdt="-")
	 */
	if (!IS_ENABLED(CONFIG_SUPPORT_PASSING_ATAGS) ||
	    !label->fdt || strcmp("-", label->fdt)) {
		conf_fdt_str = env_get("fdt_addr");
		if (conf_fdt_str)
			return conf_fdt_str;
	}

	/*
	 * Fallback to fdtcontroladdr if not a FIT image and not ATAG mode
	 */
	buf = map_sysmem(kern_addr, 0);
	if (genimg_get_format(buf) != IMAGE_FORMAT_FIT) {
		if (!IS_ENABLED(CONFIG_SUPPORT_PASSING_ATAGS) ||
		    !label->fdt || strcmp("-", label->fdt)) {
			conf_fdt_str = env_get("fdtcontroladdr");
		}
	}
	unmap_sysmem(buf);

	return conf_fdt_str;
}

/**
 * label_get_fdt_path() - Get the FDT path for a label
 *
 * Determine the FDT filename from label->fdt or by constructing it from
 * label->fdtdir and environment variables.
 *
 * @label: Label to get FDT path for
 * @fdtfilep: Returns allocated FDT path, or NULL if none. Caller must free.
 * Return: 0 on success, -ENOMEM on allocation failure
 */
static int label_get_fdt_path(struct pxe_label *label, char **fdtfilep)
{
	char *fdtfile = NULL;

	if (label->fdt) {
		if (IS_ENABLED(CONFIG_SUPPORT_PASSING_ATAGS)) {
			if (strcmp("-", label->fdt))
				fdtfile = strdup(label->fdt);
		} else {
			fdtfile = strdup(label->fdt);
		}
	} else if (label->fdtdir) {
		char *f1, *f2, *f3, *f4, *slash;
		int len;

		f1 = env_get("fdtfile");
		if (f1) {
			f2 = "";
			f3 = "";
			f4 = "";
		} else {
			/*
			 * For complex cases where this code doesn't
			 * generate the correct filename, the board
			 * code should set $fdtfile during early boot,
			 * or the boot scripts should set $fdtfile
			 * before invoking "pxe" or "sysboot".
			 */
			f1 = env_get("soc");
			f2 = "-";
			f3 = env_get("board");
			f4 = ".dtb";
			if (!f1) {
				f1 = "";
				f2 = "";
			}
			if (!f3) {
				f2 = "";
				f3 = "";
			}
		}

		len = strlen(label->fdtdir);
		if (!len)
			slash = "./";
		else if (label->fdtdir[len - 1] != '/')
			slash = "/";
		else
			slash = "";

		len = strlen(label->fdtdir) + strlen(slash) +
			strlen(f1) + strlen(f2) + strlen(f3) +
			strlen(f4) + 1;
		fdtfile = malloc(len);
		if (!fdtfile) {
			printf("malloc fail (FDT filename)\n");
			return -ENOMEM;
		}

		snprintf(fdtfile, len, "%s%s%s%s%s%s",
			 label->fdtdir, slash, f1, f2, f3, f4);
	}

	*fdtfilep = fdtfile;

	return 0;
}

/**
 * label_process_fdt() - Process FDT after loading
 *
 * Set the working FDT address, handle kaslrseed, and apply overlays.
 * The FDT must already be loaded (ctx->fdt_addr set by pxe_load_files()).
 *
 * @ctx: PXE context
 * @label: Label to process
 * Return: 0 if OK
 */
static int label_process_fdt(struct pxe_context *ctx, struct pxe_label *label)
{
	if (!ctx->fdt_addr)
		return 0;

	ctx->fdt = map_sysmem(ctx->fdt_addr, 0);

	if (label->kaslrseed)
		label_boot_kaslrseed(ctx);

	if (IS_ENABLED(CONFIG_OF_LIBFDT_OVERLAY) && label->files.count)
		label_apply_fdtoverlays(ctx, label);
	label_boot_extension(ctx, label);

	return 0;
}
/**
 * label_run_boot() - Set up the FDT and call the appropriate bootm/z/i command
 *
 * @ctx: PXE context
 * @label: Label to process
 * @kern_addr_str: String containing kernel address and possible FIT
 * configuration (cannot be NULL)
 * @kern_addr: Kernel address (cannot be 0)
 * @kern_size: Kernel size in bytes
 * @initrd_addr: String containing initrd address (0 if none)
 * @initrd_size: initrd size (only used if @initrd_addr)
 * @initrd_str: initrd string to process (only used if @initrd_addr)
 * @conf_fdt_str: string containing the FDT address
 * @conf_fdt: FDT address (0 if none)
 * Return: does not return on success, or returns 0 if the boot command
 * returned, or -ve error value on error
 */
static int label_run_boot(struct pxe_context *ctx, struct pxe_label *label,
			  char *kern_addr_str, ulong kern_addr, ulong kern_size,
			  ulong initrd_addr, ulong initrd_size,
			  char *initrd_str, const char *conf_fdt_str,
			  ulong conf_fdt)
{
	struct bootm_info bmi;
	int ret = 0;
	void *buf;
	enum image_fmt_t  fmt;

	log_debug("label '%s' kern_addr_str '%s' kern_addr %lx initrd_addr %lx "
		  "initrd_size %lx initrd_str '%s' conf_fdt_str '%s' "
		  "conf_fdt %lx\n", label->name, kern_addr_str, kern_addr,
		  initrd_addr, initrd_size, initrd_str, conf_fdt_str, conf_fdt);

	bootm_init(&bmi);

	bmi.addr_img = kern_addr_str;
	bmi.conf_fdt = conf_fdt_str;
	bootm_x86_set(&bmi, bzimage_addr, hextoul(kern_addr_str, NULL));
	bmi.os_size = kern_size;

	if (initrd_addr) {
		bmi.conf_ramdisk = initrd_str;
		bootm_x86_set(&bmi, initrd_addr, initrd_addr);
		bootm_x86_set(&bmi, initrd_size, initrd_size);
	}

	buf = map_sysmem(kern_addr, 0);

	/*
	 * Try bootm for legacy and FIT format image, assume booti if
	 * compressed
	 */
	fmt = genimg_get_format_comp(buf);

	if (IS_ENABLED(CONFIG_CMD_BOOTM) && (fmt == IMAGE_FORMAT_FIT ||
	    fmt == IMAGE_FORMAT_LEGACY)) {
		int states;

		states = ctx->restart ? BOOTM_STATE_RESTART : BOOTM_STATE_START;
		log_debug("using bootm fake_go=%d\n", ctx->fake_go);
		if (ctx->fake_go)
			states |= BOOTM_STATE_OS_FAKE_GO;
		else
			states |= BOOTM_STATE_OS_GO;
		ret = boot_run(&bmi, "ext", states | BOOTM_STATE_FINDOS |
			BOOTM_STATE_PRE_LOAD | BOOTM_STATE_FINDOTHER |
			BOOTM_STATE_LOADOS);
	/* Try booting an AArch64 Linux kernel image */
	} else if (CONFIG_IS_ENABLED(LIB_BOOTI) && fmt == IMAGE_FORMAT_BOOTI) {
		log_debug("using booti\n");
		ret = booti_run(&bmi);
	/* Try booting a Image */
	} else if (CONFIG_IS_ENABLED(LIB_BOOTZ)) {
		log_debug("using bootz\n");
		ret = bootz_run(&bmi);
	/* Try booting an x86_64 Linux kernel image */
	} else if (IS_ENABLED(CONFIG_ZBOOT)) {
		log_debug("using zboot\n");
		ret = zboot_run(&bmi);
	}

	unmap_sysmem(buf);
	if (ret)
		return ret;

	return 0;
}

/**
 * generate_localboot() - Try to come up with a localboot definition
 *
 * Adds a default kernel and initrd filename for use with localboot
 *
 * @label: Label to process
 * Return 0 if OK, -ENOMEM if out of memory
 */
static int generate_localboot(struct pxe_label *label)
{
	char *initrd_path;

	label->kernel = strdup("/vmlinuz");
	label->kernel_label = strdup(label->kernel);
	initrd_path = strdup("/initrd.img");
	if (!label->kernel || !label->kernel_label || !initrd_path)
		return -ENOMEM;
	if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
		if (!alist_add(&label->initrds, initrd_path)) {
			free(initrd_path);
			return -ENOMEM;
		}
	} else {
		label->initrd = initrd_path;
	}

	return 0;
}

/**
 * pxe_load_initrds() - Load all initrd files consecutively
 *
 * @ctx: PXE context
 * @label: Label containing initrd file paths
 * @initrd_addr: Starting address for first initrd
 * @total_sizep: Returns total size of all initrds
 * Return: 0 on success, -EIO on error
 */
static int pxe_load_initrds(struct pxe_context *ctx, struct pxe_label *label,
			    ulong initrd_addr, ulong *total_sizep)
{
	const char **initrd_path;
	ulong total_size = 0;
	int ret;
	int i;

	/* Load each initrd consecutively */
	for (i = 0; i < label->initrds.count; i++) {
		ulong size;

		initrd_path = alist_get(&label->initrds, i, char *);
		/* Use ramdisk_addr_r for first, then append */
		if (i == 0) {
			ret = get_relfile_envaddr(ctx, *initrd_path,
						  "ramdisk_addr_r", SZ_2M,
						  (enum bootflow_img_t)IH_TYPE_RAMDISK,
						  &initrd_addr, &size);
			ctx->initrd_addr = initrd_addr;
		} else {
			/* Load subsequent initrds after the previous one */
			ulong addr = initrd_addr + total_size;

			ret = get_relfile_envaddr(ctx, *initrd_path,
						  NULL, SZ_2M,
						  (enum bootflow_img_t)IH_TYPE_RAMDISK,
						  &addr, &size);
		}
		if (ret < 0) {
			printf("Skipping %s for failure retrieving initrd %s\n",
			       label->name, *initrd_path);
			return -EIO;
		}
		total_size += size;
	}
	*total_sizep = total_size;

	return 0;
}

int pxe_load_files(struct pxe_context *ctx, struct pxe_label *label,
		   char *fdtfile)
{
	const char **initrd_path;
	ulong initrd_addr = 0;
	ulong total_size = 0;
	int ret;

	if (!label->kernel) {
		printf("No kernel given, skipping %s\n", label->name);
		return -ENOENT;
	}

	if (get_relfile_envaddr(ctx, label->kernel, "kernel_addr_r", SZ_2M,
				(enum bootflow_img_t)IH_TYPE_KERNEL,
				&ctx->kern_addr, &ctx->kern_size) < 0) {
		printf("Skipping %s for failure retrieving kernel\n",
		       label->name);
		return -EIO;
	}

	/* Load initrds if present */
	if (IS_ENABLED(CONFIG_PXE_INITRD_LIST)) {
		if (label->initrds.count) {
			/* For FIT, check if first initrd is identical to kernel */
			initrd_path = alist_get(&label->initrds, 0, char *);
			if (!strcmp(label->kernel_label, *initrd_path)) {
				ctx->initrd_addr = ctx->kern_addr;
				ctx->initrd_size = ctx->kern_size;
			} else {
				ret = pxe_load_initrds(ctx, label, initrd_addr,
						       &total_size);
				if (ret)
					return ret;
				ctx->initrd_size = total_size;
			}
		}
	} else {
		if (label->initrd) {
			if (!strcmp(label->kernel_label, label->initrd)) {
				ctx->initrd_addr = ctx->kern_addr;
				ctx->initrd_size = ctx->kern_size;
			} else {
				if (get_relfile_envaddr(ctx, label->initrd,
							"ramdisk_addr_r", SZ_2M,
							(enum bootflow_img_t)IH_TYPE_RAMDISK,
							&ctx->initrd_addr,
							&ctx->initrd_size) < 0) {
					printf("Skipping %s for failure retrieving initrd\n",
					       label->name);
					return -EIO;
				}
			}
		}
	}

	if (fdtfile) {
		ret = get_relfile_envaddr(ctx, fdtfile, "fdt_addr_r", SZ_4K,
					  (enum bootflow_img_t)IH_TYPE_FLATDT,
					  &ctx->fdt_addr, NULL);
		free(fdtfile);
		if (ret < 0) {
			if (label->fdt) {
				printf("Skipping %s for failure retrieving FDT\n",
				       label->name);
				return -ENOENT;
			}

			if (label->fdtdir) {
				printf("Skipping fdtdir %s for failure retrieving dts\n",
				       label->fdtdir);
			}
		}
	}

	if (IS_ENABLED(CONFIG_OF_LIBFDT_OVERLAY) && label->files.count)
		label_load_fdtoverlays(ctx, label);

	return 0;
}

int pxe_load_label(struct pxe_context *ctx, struct pxe_label *label)
{
	char *fdtfile = NULL;
	bool is_fit;
	int ret;

	if (label->localboot) {
		if (label->localboot_val >= 0) {
			if (IS_ENABLED(CONFIG_BOOTMETH_EXTLINUX_LOCALBOOT) &&
			    !label->kernel) {
				ret = generate_localboot(label);
				if (ret)
					return ret;
			}
		}
		/* negative localboot_val means skip loading */
		if (!label->kernel)
			return 0;
	}

	/* Check for FIT case: FDT comes from FIT image, not a separate file */
	is_fit = label->fdt && label->kernel_label &&
		 !strcmp(label->kernel_label, label->fdt);

	if (!is_fit) {
		ret = label_get_fdt_path(label, &fdtfile);
		if (ret)
			return ret;
	}

	ret = pxe_load_files(ctx, label, fdtfile);
	if (ret)
		return ret;

	/* Copy fdt_addr to conf_fdt for callers that don't use pxe_setup_label */
	ctx->conf_fdt = ctx->fdt_addr;

	return 0;
}

int pxe_setup_label(struct pxe_context *ctx, struct pxe_label *label)
{
	char fit_addr[200];
	const char *conf_fdt_str;
	ulong conf_fdt = 0;
	char initrd_str[28] = "";
	bool is_fit;
	int ret;

	/* Check for FIT case: FDT comes from FIT image, not a separate file */
	is_fit = label->fdt && label->kernel_label &&
		 !strcmp(label->kernel_label, label->fdt);

	/* for FIT, append the configuration identifier */
	snprintf(fit_addr, sizeof(fit_addr), "%lx%s", ctx->kern_addr,
		 label->config ? label->config : "");

	if (ctx->initrd_addr && ctx->initrd_size) {
		int size;

		size = snprintf(initrd_str, sizeof(initrd_str), "%lx:%lx",
				ctx->initrd_addr, ctx->initrd_size);
		if (size >= sizeof(initrd_str))
			return -ENOSPC;
	}

	/*
	 * FDT handling has several scenarios:
	 *
	 * 1. FIT image with embedded FDT: label->fdt matches kernel_label,
	 *    use the FIT address so bootm extracts the FDT from the FIT
	 *
	 * 2. Separate FDT file: if fdt_addr_r is set and "fdt" or "fdtdir"
	 *    is specified, load the FDT from the server
	 *
	 * 3. Fallback to fdt_addr env var if set
	 *
	 * 4. Fallback to fdtcontroladdr for non-FIT images
	 *
	 * 5. No FDT available
	 */
	conf_fdt_str = env_get("fdt_addr_r");
	log_debug("label '%s' kernel_addr '%s' label->fdt '%s' fdtdir '%s' kernel_label '%s' fdt_argp '%s'\n",
		  label->name, fit_addr, label->fdt, label->fdtdir,
		  label->kernel_label, conf_fdt_str);

	/* Scenario 1: FIT with embedded FDT */
	if (is_fit) {
		conf_fdt_str = fit_addr;
	} else if (ctx->fdt_addr) {
		/* Scenario 2: FDT loaded by pxe_load_files(), do post-processing */
		ret = label_process_fdt(ctx, label);
		if (ret)
			return ret;
		conf_fdt_str = env_get("fdt_addr_r");
	} else {
		/* No FDT specified, use fallback */
		conf_fdt_str = NULL;
	}

	/* Scenarios 3 and 4: fallback options */
	if (!conf_fdt_str)
		conf_fdt_str = pxe_get_fdt_fallback(label, ctx->kern_addr);
	if (conf_fdt_str)
		conf_fdt = hextoul(conf_fdt_str, NULL);
	log_debug("conf_fdt %lx\n", conf_fdt);

	if (ctx->bflow && conf_fdt_str)
		ctx->bflow->fdt_addr = conf_fdt;

	/* Save the loaded info to context */
	ctx->label = label;
	ctx->kern_addr_str = strdup(fit_addr);
	if (ctx->initrd_addr)
		ctx->initrd_str = strdup(initrd_str);
	ctx->conf_fdt_str = strdup(conf_fdt_str);
	ctx->conf_fdt = conf_fdt;

	log_debug("Loaded label '%s':\n", label->name);
	log_debug("- kern_addr_str '%s' conf_fdt_str '%s' conf_fdt %lx\n",
		  ctx->kern_addr_str, ctx->conf_fdt_str, conf_fdt);
	if (ctx->initrd_addr) {
		log_debug("- initrd addr %lx filesize %lx str '%s'\n",
			  ctx->initrd_addr, ctx->initrd_size, ctx->initrd_str);
	}
	if (!ctx->kern_addr_str || (conf_fdt_str && !ctx->conf_fdt_str) ||
	    (ctx->initrd_addr && !ctx->initrd_str)) {
		printf("malloc fail (saving label)\n");
		return -ENOMEM;
	}

	return 0;
}

/**
 * label_boot_prep() - Prepare a label for booting
 *
 * Handles localboot directive if present and loads all files needed for boot
 * (kernel, initrd, FDT, overlays). For positive localboot_val, attempts
 * localboot and falls back to generate_localboot() if needed. For negative
 * localboot_val, indicates the label should be skipped.
 *
 * @ctx: PXE context
 * @label: Label to process
 * Return: 0 to continue booting, 1 to skip this label, -ve on error
 */
static int label_boot_prep(struct pxe_context *ctx, struct pxe_label *label)
{
	int ret;

	if (label->localboot) {
		if (label->localboot_val >= 0) {
			ret = label_localboot(label);

			if (IS_ENABLED(CONFIG_BOOTMETH_EXTLINUX_LOCALBOOT) &&
			    ret == -ENOENT)
				ret = generate_localboot(label);
			if (ret)
				return ret;
		} else {
			return 1;  /* skip this label */
		}
	}

	/* Load files if not already done */
	if (!ctx->label) {
		ret = pxe_load_label(ctx, label);
		if (ret)
			return 1;
	}

	return 0;
}

/**
 * label_boot() - Boot according to the contents of a pxe_label
 *
 * If we can't boot for any reason, we return. A successful boot never returns.
 *
 * The kernel will be stored in the location given by the 'kernel_addr_r'
 * environment variable.
 *
 * If the label specifies an initrd file, it will be stored in the location
 * given by the 'ramdisk_addr_r' environment variable.
 *
 * If the label specifies an 'append' line, its contents will overwrite that
 * of the 'bootargs' environment variable.
 *
 * @ctx: PXE context
 * @label: Label to process
 * Return: does not return on success, otherwise returns 0 if a localboot
 *	label was processed, or 1 on error
 */
static int label_boot(struct pxe_context *ctx, struct pxe_label *label)
{
	char mac_str[29] = "";
	char ip_str[68] = "";
	int ret;

	if (label->say)
		printf("%s\n", label->say);

	label_print(label);

	label->attempted = 1;

	ret = label_boot_prep(ctx, label);
	if (ret)
		return ret > 0 ? 0 : ret;

	/* Set up boot params if not already done */
	if (!ctx->label) {
		ret = pxe_setup_label(ctx, label);
		if (ret)
			return 1;
	}

	if (label->ipappend & 0x1) {
		sprintf(ip_str, " ip=%s:%s:%s:%s",
			env_get("ipaddr"), env_get("serverip"),
			env_get("gatewayip"), env_get("netmask"));
	}

	if (IS_ENABLED(CONFIG_CMD_NET))	{
		if (label->ipappend & 0x2) {
			int err;

			strcpy(mac_str, " BOOTIF=");
			err = format_mac_pxe(mac_str + 8, sizeof(mac_str) - 8);
			if (err < 0)
				mac_str[0] = '\0';
		}
	}

	if ((label->ipappend & 0x3) || label->append) {
		char bootargs[CONFIG_SYS_CBSIZE] = "";
		char finalbootargs[CONFIG_SYS_CBSIZE];

		if (strlen(label->append ?: "") +
		    strlen(ip_str) + strlen(mac_str) + 1 > sizeof(bootargs)) {
			printf("bootarg overflow %zd+%zd+%zd+1 > %zd\n",
			       strlen(label->append ?: ""),
			       strlen(ip_str), strlen(mac_str),
			       sizeof(bootargs));
			return 1;
		}

		if (label->append)
			strlcpy(bootargs, label->append, sizeof(bootargs));

		strcat(bootargs, ip_str);
		strcat(bootargs, mac_str);

		cli_simple_process_macros(bootargs, finalbootargs,
					  sizeof(finalbootargs));
		env_set("bootargs", finalbootargs);
		printf("append: %s\n", finalbootargs);
	}

	if (IS_ENABLED(CONFIG_BOOTSTD_FULL) && ctx->no_boot)
		return 0;

	label_run_boot(ctx, label, ctx->kern_addr_str, ctx->kern_addr,
		       ctx->kern_size, ctx->initrd_addr, ctx->initrd_size,
		       ctx->initrd_str, ctx->conf_fdt_str, ctx->conf_fdt);
	/* ignore the error value since we are going to fail anyway */

	/*
	 * Sandbox cannot boot a real kernel, so stop after the first attempt.
	 * On real hardware, returning is always failure, so try next label.
	 */
	if (IS_ENABLED(CONFIG_SANDBOX))
		return 0;

	return 1;
}

struct pxe_menu *pxe_menu_init(void)
{
	struct pxe_menu *cfg;

	cfg = malloc(sizeof(struct pxe_menu));
	if (!cfg)
		return NULL;

	memset(cfg, '\0', sizeof(struct pxe_menu));
	INIT_LIST_HEAD(&cfg->labels);
	alist_init(&cfg->includes, sizeof(struct pxe_include), 0);

	return cfg;
}

void pxe_menu_uninit(struct pxe_menu *cfg)
{
	struct pxe_include *inc;
	struct list_head *pos, *n;
	struct pxe_label *label;

	free(cfg->title);
	free(cfg->default_label);
	free(cfg->fallback_label);
	free(cfg->bmp);

	list_for_each_safe(pos, n, &cfg->labels) {
		label = list_entry(pos, struct pxe_label, list);

		label_destroy(label);
	}

	alist_for_each(inc, &cfg->includes)
		free(inc->path);
	alist_uninit(&cfg->includes);

	free(cfg);
}

struct pxe_menu *parse_pxefile(struct pxe_context *ctx, struct abuf *buf)
{
	struct pxe_menu *cfg;
	char *base;
	int r;

	cfg = pxe_menu_init();
	if (!cfg)
		return NULL;

	base = abuf_data(buf);
	r = parse_pxefile_top(ctx, base, base + buf->size, cfg, 1);

	if (r < 0) {
		pxe_menu_uninit(cfg);
		return NULL;
	}

	if (ctx->use_fallback) {
		if (cfg->fallback_label) {
			printf("Setting use of fallback\n");
			cfg->default_label = cfg->fallback_label;
		} else {
			printf("Selected fallback option, but not set\n");
		}
	}

	return cfg;
}

int pxe_process_includes(struct pxe_context *ctx, struct pxe_menu *cfg,
			 ulong base)
{
	struct pxe_include *inc;
	uint i;
	int r;

	/*
	 * Process includes - load each file and parse it. Get the include
	 * fresh each iteration since parsing may add more includes and cause
	 * alist reallocation.
	 */
	for (i = 0; i < cfg->includes.count; i++) {
		inc = alist_getw(&cfg->includes, i, struct pxe_include);

		r = get_pxe_file(ctx, inc->path, base);
		if (r < 0) {
			printf("Couldn't retrieve %s\n", inc->path);
			return r;
		}

		r = pxe_parse_include(ctx, inc, base, ctx->pxe_file_size);

		if (r < 0)
			return r;
	}

	return 0;
}

int pxe_parse_include(struct pxe_context *ctx, const struct pxe_include *inc,
		      ulong addr, ulong size)
{
	char *buf;
	int ret;

	buf = map_sysmem(addr, size);
	ret = parse_pxefile_top(ctx, buf, buf + size, inc->cfg,
				inc->nest_level);
	unmap_sysmem(buf);

	return ret;
}

/*
 * Converts a pxe_menu struct into a menu struct for use with U-Boot's generic
 * menu code.
 */
static struct menu *pxe_menu_to_menu(struct pxe_menu *cfg)
{
	struct pxe_label *label;
	struct list_head *pos;
	struct menu *m;
	char *label_override;
	int err;
	int i = 1;
	char *default_num = NULL;
	char *override_num = NULL;
	int timeout;

	timeout = env_get_ulong("pxe_timeout", 10, DIV_ROUND_UP(cfg->timeout, 10));

	/*
	 * Create a menu and add items for all the labels.
	 */
	m = menu_create(cfg->title, timeout,
			cfg->prompt, NULL, label_print, NULL, NULL, NULL);
	if (!m)
		return NULL;

	label_override = env_get("pxe_label_override");

	list_for_each(pos, &cfg->labels) {
		label = list_entry(pos, struct pxe_label, list);

		sprintf(label->num, "%d", i++);
		if (menu_item_add(m, label->num, label) != 1) {
			menu_destroy(m);
			return NULL;
		}
		if (cfg->default_label &&
		    (strcmp(label->name, cfg->default_label) == 0))
			default_num = label->num;
		if (label_override && !strcmp(label->name, label_override))
			override_num = label->num;
	}

	if (label_override) {
		if (override_num)
			default_num = override_num;
		else
			printf("Missing override pxe label: %s\n",
			      label_override);
	}

	/*
	 * After we've created items for each label in the menu, set the
	 * menu's default label if one was specified.
	 */
	if (default_num) {
		err = menu_default_set(m, default_num);
		if (err != 1) {
			if (err != -ENOENT) {
				menu_destroy(m);
				return NULL;
			}

			printf("Missing default: %s\n", cfg->default_label);
		}
	}

	return m;
}

/*
 * Try to boot any labels we have yet to attempt to boot.
 */
static void boot_unattempted_labels(struct pxe_context *ctx,
				    struct pxe_menu *cfg)
{
	struct list_head *pos;
	struct pxe_label *label;

	log_debug("Booting unattempted labels\n");
	list_for_each(pos, &cfg->labels) {
		label = list_entry(pos, struct pxe_label, list);

		if (!label->attempted) {
			log_debug("attempt: %s\n", label->name);
			if (!label_boot(ctx, label))
				return;
		}
	}
}

int pxe_select_label(struct pxe_menu *cfg, bool prompt,
		     struct pxe_label **labelp)
{
	void *choice;
	struct menu *m;
	int err;

	if (prompt)
		cfg->prompt = 1;

	m = pxe_menu_to_menu(cfg);
	if (!m)
		return -ENOMEM;

	err = menu_get_choice(m, &choice);
	menu_destroy(m);

	/*
	 * err == 1 means we got a choice back from menu_get_choice.
	 *
	 * err == -ENOENT if the menu was setup to select the default but no
	 * default was set.
	 *
	 * otherwise, the user interrupted or there was some other error.
	 */
	if (err == 1) {
		*labelp = choice;
		return 0;
	}

	return err == -ENOENT ? -ENOENT : -ECANCELED;
}

void handle_pxe_menu(struct pxe_context *ctx, struct pxe_menu *cfg)
{
	struct pxe_label *label;
	int err;

	if (IS_ENABLED(CONFIG_CMD_BMP)) {
		/* display BMP if available */
		if (cfg->bmp) {
			if (get_relfile(ctx, cfg->bmp, &image_load_addr, 0,
					BFI_LOGO, NULL)) {
#if defined(CONFIG_VIDEO)
				struct udevice *dev;

				err = uclass_first_device_err(UCLASS_VIDEO, &dev);
				if (!err)
					video_clear(dev);
#endif
				bmp_display(image_load_addr,
					    BMP_ALIGN_CENTER, BMP_ALIGN_CENTER);
			} else {
				printf("Skipping background bmp %s for failure\n",
				       cfg->bmp);
			}
		}
	}

	err = pxe_select_label(cfg, false, &label);

	/*
	 * err == 0 means we got a choice back.
	 *
	 * err == -ENOENT if the menu was setup to select the default but no
	 * default was set. in that case, we should continue trying to boot
	 * labels that haven't been attempted yet.
	 *
	 * otherwise, the user interrupted or there was some other error and
	 * we give up.
	 */

	if (!err) {
		err = label_boot(ctx, label);
		log_debug("label_boot() returns %d\n", err);
		if (!err)
			return;
	} else if (err != -ENOENT) {
		return;
	}

	boot_unattempted_labels(ctx, cfg);
}

int pxe_setup_ctx(struct pxe_context *ctx, pxe_getfile_func getfile,
		  void *userdata, bool allow_abs_path, const char *bootfile,
		  bool use_ipv6, bool use_fallback, struct bootflow *bflow)
{
	const char *last_slash;
	size_t path_len = 0;

	memset(ctx, '\0', sizeof(*ctx));
	ctx->getfile = getfile;
	ctx->userdata = userdata;
	ctx->allow_abs_path = allow_abs_path;
	ctx->use_ipv6 = use_ipv6;
	ctx->use_fallback = use_fallback;
	ctx->bflow = bflow;

	/* figure out the boot directory, if there is one */
	if (bootfile && strlen(bootfile) >= MAX_TFTP_PATH_LEN)
		return -ENOSPC;
	ctx->bootdir = strdup(bootfile ? bootfile : "");
	if (!ctx->bootdir)
		return -ENOMEM;

	if (bootfile) {
		last_slash = strrchr(bootfile, '/');
		if (last_slash)
			path_len = (last_slash - bootfile) + 1;
	}
	ctx->bootdir[path_len] = '\0';

	return 0;
}

void pxe_destroy_ctx(struct pxe_context *ctx)
{
	free(ctx->bootdir);
}

/**
 * pxe_prepare() - Prepare a PXE menu by parsing and processing includes
 *
 * Parses the PXE config file and processes any include directives.
 *
 * @ctx: PXE context
 * @pxefile_addr_r: Address where config file is loaded
 * @prompt: true to force the menu prompt
 * Return: Parsed menu on success, NULL on error
 */
static struct pxe_menu *pxe_prepare(struct pxe_context *ctx,
				    ulong pxefile_addr_r, bool prompt)
{
	struct pxe_menu *cfg;
	struct abuf buf;
	int ret;

	abuf_init_addr(&buf, pxefile_addr_r, ctx->pxe_file_size);
	cfg = parse_pxefile(ctx, &buf);
	if (!cfg) {
		printf("Error parsing config file\n");
		return NULL;
	}

	ret = pxe_process_includes(ctx, cfg, pxefile_addr_r);
	if (ret) {
		pxe_menu_uninit(cfg);
		return NULL;
	}

	if (prompt)
		cfg->prompt = prompt;

	return cfg;
}

struct pxe_context *pxe_parse(ulong addr, ulong size, const char *bootfile)
{
	struct pxe_context *ctx;
	struct abuf buf;
	int ret;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ret = pxe_setup_ctx(ctx, NULL, NULL, true, bootfile, false, false,
			    NULL);
	if (ret) {
		free(ctx);
		return NULL;
	}
	ctx->quiet = true;
	ctx->pxe_file_size = size;

	abuf_init_addr(&buf, addr, size);
	ctx->cfg = parse_pxefile(ctx, &buf);
	if (!ctx->cfg) {
		pxe_destroy_ctx(ctx);
		free(ctx);
		return NULL;
	}

	return ctx;
}

void pxe_load(struct pxe_file *file, ulong addr, ulong size)
{
	file->addr = addr;
	file->size = size;
}

void pxe_cleanup(struct pxe_context *ctx)
{
	if (ctx->cfg)
		pxe_menu_uninit(ctx->cfg);
	pxe_destroy_ctx(ctx);
	free(ctx);
}

int pxe_process(struct pxe_context *ctx, ulong addr, ulong size, bool prompt)
{
	struct pxe_menu *cfg;

	ctx->pxe_file_size = size;
	cfg = pxe_prepare(ctx, addr, prompt);
	if (!cfg)
		return 1;

	handle_pxe_menu(ctx, cfg);

	pxe_menu_uninit(cfg);

	return 0;
}

int pxe_process_str(struct pxe_context *ctx, ulong pxefile_addr_r, bool prompt)
{
	void *ptr;
	int len;

	ptr = map_sysmem(pxefile_addr_r, 0);
	len = strnlen(ptr, SZ_64K);
	unmap_sysmem(ptr);

	return pxe_process(ctx, pxefile_addr_r, len, prompt);
}

int pxe_probe(struct pxe_context *ctx, ulong pxefile_addr_r, bool prompt)
{
	ctx->cfg = pxe_prepare(ctx, pxefile_addr_r, prompt);
	if (!ctx->cfg)
		return -EINVAL;
	ctx->no_boot = true;

	handle_pxe_menu(ctx, ctx->cfg);

	return 0;
}

int pxe_boot(struct pxe_context *ctx)
{
	int ret;

	if (!ctx->label)
		return log_msg_ret("pxb", -ENOENT);

	ret = label_run_boot(ctx, ctx->label, ctx->kern_addr_str,
			     ctx->kern_addr, ctx->kern_size, ctx->initrd_addr,
			     ctx->initrd_size, ctx->initrd_str,
			     ctx->conf_fdt_str, ctx->conf_fdt);
	if (ret)
		return log_msg_ret("lrb", ret);

	return 0;
}

int pxe_boot_entry(struct pxe_context *ctx, ulong addr, int entry)
{
	bool free_cfg = false;
	struct pxe_label *label;
	struct pxe_menu *cfg;
	int i = 0;
	int ret;

	/* Use cached config if available, otherwise parse */
	cfg = ctx->cfg;
	if (!cfg) {
		void *ptr;

		ptr = map_sysmem(addr, 0);
		ctx->pxe_file_size = strnlen(ptr, SZ_64K);
		unmap_sysmem(ptr);

		cfg = pxe_prepare(ctx, addr, false);
		if (!cfg)
			return log_msg_ret("prp", -EINVAL);
		free_cfg = true;
	}

	list_for_each_entry(label, &cfg->labels, list) {
		if (i == entry)
			goto found;
		i++;
	}
	if (free_cfg)
		pxe_menu_uninit(cfg);
	return log_msg_ret("lab", -ENOENT);

found:
	ret = label_boot(ctx, label);
	if (free_cfg)
		pxe_menu_uninit(cfg);

	return ret;
}
