// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2013, Google Inc.
 *
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2006
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#define LOG_CATEGORY LOGC_BOOT

#ifdef USE_HOSTCC
#include "mkimage.h"
#include <time.h>
#include <linux/libfdt.h>
#else
#include <log.h>
#include <malloc.h>
#include <mapmem.h>
#include <linux/compiler.h>
#endif

#include <image.h>
#include <u-boot/crc.h>

/**
 * fit_print_init() - initialize FIT print context
 * @ctx: pointer to FIT print context to initialize
 * @fit: pointer to the FIT format image header
 *
 * This initializes a fit_print_ctx structure with the given FIT image.
 */
void fit_print_init(struct fit_print_ctx *ctx, const void *fit)
{
	ctx->fit = fit;
	ctx->indent = IMAGE_INDENT;
	ctx->tab = 17 + IMAGE_INDENT;
}

/**
 * emit_type() - print a label with indentation and padding
 * @ctx: pointer to FIT print context
 * @type: type prefix (e.g., "Hash" or "Sign")
 * @label: label suffix (e.g., "algo" or "value")
 *
 * Prints the indentation from the context, followed by two spaces, the type,
 * a space, the label, a colon, and padding to align values to ctx->tab.
 */
static void emit_type(struct fit_print_ctx *ctx, const char *type,
		      const char *label)
{
	int len;

	len = printf("%*s%s %s:", ctx->indent, "", type, label);
	printf("%*s", ctx->tab - len, "");
}

/**
 * emit_label() - print a label with indentation and padding
 * @ctx: pointer to FIT print context
 * @type: type prefix (e.g., "Hash" or "Sign")
 * @label: label suffix (e.g., "algo" or "value")
 *
 * Prints the indentation from the context, followed by two spaces, a space,
 * the label, a colon, and padding to align values to ctx->tab.
 */
static void emit_label(struct fit_print_ctx *ctx, const char *label)
{
	int len;

	len = printf("%*s%s%c", ctx->indent, "", label, *label ? ':' : ' ');
	printf("%*s", ctx->tab - len, "");
}

/**
 * emit_label_val() - print a label with value
 * @ctx: pointer to FIT print context
 * @label: label string (e.g., "Type" or "OS")
 * @val: value string to print after the label
 *
 * Prints the indentation, label with padding to ctx->tab, and the value
 * followed by a newline. This is a convenience function that combines
 * emit_label() and printf() for simple label-value pairs.
 */
static void emit_label_val(struct fit_print_ctx *ctx, const char *label,
			   const char *val)
{
	emit_label(ctx, label);
	printf("%s\n", val);
}

/**
 * emit_prop() - print a property if it exists
 * @ctx: pointer to FIT print context
 * @noffset: offset of the node containing the property
 * @prop: property name to get and print
 * @label: label string to use when printing
 *
 * Gets a property from the specified node and prints it with the given label
 * only if the property exists. This is a convenience function for optional
 * properties that should only be printed when present.
 */
static void emit_prop(struct fit_print_ctx *ctx, int noffset,
		      const char *prop, const char *label)
{
	const char *val;

	val = fdt_getprop(ctx->fit, noffset, prop, NULL);
	if (val)
		emit_label_val(ctx, label, val);
}

/**
 * emit_timestamp() - print a timestamp
 * @ctx: pointer to FIT print context
 * @noffset: offset of the node containing the timestamp
 * @label: label string to use when printing
 *
 * Gets the timestamp from the specified node and prints it with the given
 * label. If the timestamp is not available, prints "unavailable" instead.
 * This is a convenience function for printing FIT timestamps.
 */
static void emit_timestamp(struct fit_print_ctx *ctx, int noffset,
			   const char *label)
{
	time_t timestamp;

	if (!IMAGE_ENABLE_TIMESTAMP)
		return;
	emit_label(ctx, label);
	if (fit_get_timestamp(ctx->fit, noffset, &timestamp))
		printf("unavailable\n");
	else
		genimg_print_time(timestamp);
}

/**
 * emit_stringlist() - print a stringlist property
 * @ctx: pointer to FIT print context
 * @noffset: offset of the node containing the property
 * @prop: property name to get and print
 * @label: label string to use when printing
 *
 * Gets a stringlist property from the specified node and prints each string
 * with the given label. The first string shows the label, subsequent strings
 * are indented to align with the first value. If the property doesn't exist,
 * nothing is printed.
 */
static void emit_stringlist(struct fit_print_ctx *ctx, int noffset,
			    const char *prop, const char *label)
{
	const char *val;
	int i;

	for (i = 0;
	     val = fdt_stringlist_get(ctx->fit, noffset, prop, i, NULL), val;
	     i++)
		emit_label_val(ctx, i ? "" : label, val);
}

/**
 * emit_desc() - print a description
 * @ctx: pointer to FIT print context
 * @noffset: offset of the node containing the description
 * @label: label string to use when printing
 *
 * Gets the description from the specified node and prints it with the given
 * label. If the description is not available, prints "unavailable" instead.
 * This is a convenience function for printing FIT descriptions.
 */
static void emit_desc(struct fit_print_ctx *ctx, int noffset,
		      const char *label)
{
	const char *desc;
	int ret;

	ret = fit_get_desc(ctx->fit, noffset, &desc);
	emit_label_val(ctx, label, ret ? "unavailable" : desc);
}

/**
 * emit_addr() - print an address property
 * @ctx: pointer to FIT print context
 * @label: label string to use when printing
 * @addr: address value to print
 * @valid: true if the address is valid, false to print "unavailable"
 *
 * Prints an address with the given label. If valid is false, prints
 * "unavailable" instead of the address value.
 */
static void emit_addr(struct fit_print_ctx *ctx, const char *label, ulong addr,
		      bool valid)
{
	emit_label(ctx, label);
	if (valid)
		printf("0x%08lx\n", addr);
	else
		printf("unavailable\n");
}

/**
 * fit_image_print_data() - prints out the hash node details
 * @ctx: pointer to FIT print context
 * @noffset: offset of the hash node
 * @type: Type of information to print ("hash" or "sign")
 *
 * fit_image_print_data() lists properties for the processed hash node
 *
 * This function avoid using puts() since it prints a newline on the host
 * but does not in U-Boot.
 *
 * returns:
 *     no returned results
 */
static void fit_image_print_data(struct fit_print_ctx *ctx, int noffset,
				 const char *type)
{
	const char *keyname, *padding, *algo;
	int p = ctx->indent;
	const void *fit = ctx->fit;
	int value_len, ret, i;
	uint8_t *value;

	debug("%s node:    '%s'\n", type, fit_get_name(fit, noffset));
	emit_type(ctx, type, "algo");
	if (fit_image_hash_get_algo(fit, noffset, &algo)) {
		printf("invalid/unsupported\n");
		return;
	}
	printf("%s", algo);
	keyname = fdt_getprop(fit, noffset, FIT_KEY_HINT, NULL);
	if (keyname)
		printf(":%s", keyname);
	printf("\n");

	padding = fdt_getprop(fit, noffset, "padding", NULL);
	if (padding)
		printf("%*s%s padding:  %s\n", p, "", type, padding);

	ret = fit_image_hash_get_value(fit, noffset, &value, &value_len);
	emit_type(ctx, type, "value");
	if (ret) {
		printf("unavailable\n");
	} else {
		for (i = 0; i < value_len; i++)
			printf("%02x", value[i]);
		printf("\n");
	}

	debug("%s len:     %d\n", type, value_len);

	/* Signatures have a time stamp */
	if (IMAGE_ENABLE_TIMESTAMP && keyname)
		emit_timestamp(ctx, noffset, "Timestamp");
}

/**
 * fit_image_print_dm_verity() - prints out the dm-verity node details
 * @ctx: pointer to FIT print context
 * @noffset: offset of the dm-verity node
 *
 * Prints the hash algorithm, root digest and salt from a dm-verity
 * subnode, if present.
 */
static __maybe_unused void fit_image_print_dm_verity(struct fit_print_ctx *ctx,
						     int noffset)
{
#if defined(USE_HOSTCC) || CONFIG_IS_ENABLED(FIT_VERITY)
	const void *fit = ctx->fit;
	const char *algo;
	const uint8_t *bin;
	int len, i;

	algo = fdt_getprop(fit, noffset, FIT_VERITY_ALGO_PROP, NULL);
	if (algo) {
		emit_type(ctx, "Verity", "algo");
		printf("%s\n", algo);
	}

	bin = fdt_getprop(fit, noffset, FIT_VERITY_DIGEST_PROP, &len);
	if (bin && len > 0) {
		emit_type(ctx, "Verity", "hash");
		for (i = 0; i < len; i++)
			printf("%02x", bin[i]);
		printf("\n");
	}

	bin = fdt_getprop(fit, noffset, FIT_VERITY_SALT_PROP, &len);
	if (bin && len > 0) {
		emit_type(ctx, "Verity", "salt");
		for (i = 0; i < len; i++)
			printf("%02x", bin[i]);
		printf("\n");
	}
#endif
}

/**
 * fit_image_print_verification_data() - prints out the hash/signature details
 * @ctx: pointer to FIT print context
 * @noffset: offset of the hash or signature node
 *
 * This lists properties for the processed hash node
 *
 * returns:
 *     no returned results
 */
static void fit_image_print_verification_data(struct fit_print_ctx *ctx,
					      int noffset)
{
	const void *fit = ctx->fit;
	const char *name;

	/*
	 * Check subnode name, must be equal to "hash" or "signature".
	 * Multiple hash/signature nodes require unique unit node
	 * names, e.g. hash-1, hash-2, signature-1, signature-2, etc.
	 */
	name = fit_get_name(fit, noffset);
	if (!strncmp(name, FIT_HASH_NODENAME, strlen(FIT_HASH_NODENAME)))
		fit_image_print_data(ctx, noffset, "Hash");
	else if (!strncmp(name, FIT_SIG_NODENAME, strlen(FIT_SIG_NODENAME)))
		fit_image_print_data(ctx, noffset, "Sign");
#if defined(USE_HOSTCC) || CONFIG_IS_ENABLED(FIT_VERITY)
	else if (!strcmp(name, FIT_VERITY_NODENAME))
		fit_image_print_dm_verity(ctx, noffset);
#endif
}

/**
 * process_subnodes() - process and print verification data for all subnodes
 * @ctx: pointer to FIT print context
 * @parent: parent node offset
 *
 * Iterates through all direct child nodes of the parent and prints their
 * verification data (hash/signature information).
 */
static void process_subnodes(struct fit_print_ctx *ctx, int parent)
{
	const void *fit = ctx->fit;
	int noffset;

	fdt_for_each_subnode(noffset, fit, parent)
		fit_image_print_verification_data(ctx, noffset);
}

/**
 * fit_image_print - prints out the FIT component image details
 * @ctx: pointer to FIT print context
 * @image_noffset: offset of the component image node
 * @p: pointer to prefix string
 *
 * fit_image_print() lists all mandatory properties for the processed component
 * image. If present, hash nodes are printed out as well. Load
 * address for images of type firmware is also printed out. Since the load
 * address is not mandatory for firmware images, it will be output as
 * "unavailable" when not present.
 *
 * returns:
 *     no returned results
 */
void fit_image_print(struct fit_print_ctx *ctx, int image_noffset)
{
	const void *fit = ctx->fit;
	uint8_t type, arch, os, comp = IH_COMP_NONE;
	size_t size;
	ulong load, entry;
	const void *data;
	int ret;

	/* Mandatory properties */
	emit_desc(ctx, image_noffset, "Description");

	emit_timestamp(ctx, 0, "Created");

	fit_image_get_type(fit, image_noffset, &type);
	emit_label_val(ctx, "Type", genimg_get_type_name(type));

	fit_image_get_comp(fit, image_noffset, &comp);
	emit_label_val(ctx, "Compression", genimg_get_comp_name(comp));

	ret = fit_image_get_data(fit, image_noffset, &data, &size);

	if (!tools_build()) {
		emit_label(ctx, "Data Start");
		if (ret) {
			printf("unavailable\n");
		} else {
			void *vdata = (void *)data;

			printf("0x%08lx\n", (ulong)map_to_sysmem(vdata));
		}
	}

	emit_label(ctx, "Data Size");
	if (ret)
		printf("unavailable\n");
	else
		genimg_print_size(size);

	/* Remaining, type dependent properties */
	bool loadable = type == IH_TYPE_KERNEL || type == IH_TYPE_STANDALONE ||
			type == IH_TYPE_RAMDISK;

	if (loadable || type == IH_TYPE_FIRMWARE || type == IH_TYPE_FLATDT) {
		fit_image_get_arch(fit, image_noffset, &arch);
		emit_label_val(ctx, "Architecture", genimg_get_arch_name(arch));
	}

	if (loadable || type == IH_TYPE_FIRMWARE) {
		fit_image_get_os(fit, image_noffset, &os);
		emit_label_val(ctx, "OS", genimg_get_os_name(os));
	}

	if (loadable || type == IH_TYPE_FIRMWARE || type == IH_TYPE_FPGA ||
	    type == IH_TYPE_FLATDT) {
		ret = fit_image_get_load(fit, image_noffset, &load);
		if (type != IH_TYPE_FLATDT || !ret)
			emit_addr(ctx, "Load Address", load, !ret);
	}

	if (loadable) {
		ret = fit_image_get_entry(fit, image_noffset, &entry);
		emit_addr(ctx, "Entry Point", entry, !ret);
	}

	/* Process all hash subnodes of the component image node */
	process_subnodes(ctx, image_noffset);
}

/**
 * fit_conf_print - prints out the FIT configuration details
 * @ctx: pointer to FIT print context
 * @noffset: offset of the configuration node
 *
 * fit_conf_print() lists all mandatory properties for the processed
 * configuration node.
 *
 * returns:
 *     no returned results
 */
static void fit_conf_print(struct fit_print_ctx *ctx, int noffset)
{
	const void *fit = ctx->fit;
	const char *uname;

	/* Mandatory properties */
	emit_desc(ctx, noffset, "Description");

	uname = fdt_getprop(fit, noffset, FIT_KERNEL_PROP, NULL);
	emit_label_val(ctx, "Kernel", uname ?: "unavailable");

	/* Optional properties */
	emit_prop(ctx, noffset, FIT_RAMDISK_PROP, "Init Ramdisk");
	emit_prop(ctx, noffset, FIT_FIRMWARE_PROP, "Firmware");
	emit_stringlist(ctx, noffset, FIT_FDT_PROP, "FDT");
	emit_prop(ctx, noffset, FIT_FPGA_PROP, "FPGA");
	emit_stringlist(ctx, noffset, FIT_LOADABLE_PROP, "Loadables");
	emit_stringlist(ctx, noffset, FIT_COMPATIBLE_PROP, "Compatible");

	/* Process all hash subnodes of the component configuration node */
	process_subnodes(ctx, noffset);
}

void fit_print(struct fit_print_ctx *ctx)
{
	const void *fit = ctx->fit;
	int p = ctx->indent;
	char *uname;
	int images_noffset;
	int confs_noffset;
	int noffset;
	int count;

	/* Root node properties */
	emit_desc(ctx, 0, "FIT description");

	emit_timestamp(ctx, 0, "Created");

	/* Find images parent node offset */
	images_noffset = fdt_path_offset(fit, FIT_IMAGES_PATH);
	if (images_noffset < 0) {
		printf("Can't find images parent node '%s' (%s)\n",
		       FIT_IMAGES_PATH, fdt_strerror(images_noffset));
		return;
	}

	/* Process its subnodes, print out component images details */
	count = 0;
	fdt_for_each_subnode(noffset, fit, images_noffset) {
		/*
		 * Direct child node of the images parent node,
		 * i.e. component image node.
		 */
		printf("%*s Image %u (%s)\n", p, "", count++,
		       fit_get_name(fit, noffset));

		ctx->indent += 2;
		fit_image_print(ctx, noffset);
		ctx->indent -= 2;
	}

	/* Find configurations parent node offset */
	confs_noffset = fdt_path_offset(fit, FIT_CONFS_PATH);
	if (confs_noffset < 0) {
		debug("Can't get configurations parent node '%s' (%s)\n",
		      FIT_CONFS_PATH, fdt_strerror(confs_noffset));
		return;
	}

	/* get default configuration unit name from default property */
	uname = (char *)fdt_getprop(fit, confs_noffset, FIT_DEFAULT_PROP, NULL);
	if (uname)
		printf("%*s Default Configuration: '%s'\n", p, "", uname);

	/* Process its subnodes, print out configurations details */
	count = 0;
	fdt_for_each_subnode(noffset, fit, confs_noffset) {
		/*
		 * Direct child node of the configurations parent node,
		 * i.e. configuration node.
		 */
		printf("%*s Configuration %u (%s)\n", p, "", count++,
		       fit_get_name(fit, noffset));

		ctx->indent += 2;
		fit_conf_print(ctx, noffset);
		ctx->indent -= 2;
	}
}

void fit_print_contents(const void *fit)
{
	struct fit_print_ctx ctx;

	fit_print_init(&ctx, fit);
	fit_print(&ctx);
}
