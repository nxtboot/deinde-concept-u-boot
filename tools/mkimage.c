// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2008 Semihalf
 *
 * (C) Copyright 2000-2009
 * DENX Software Engineering
 * Wolfgang Denk, wd@denx.de
 */

#include "imagetool.h"
#include "mkimage.h"
#include "imximage.h"
#include <fit_common.h>
#include <getopt.h>
#include <image.h>
#include <version.h>
#ifdef __linux__
#include <sys/ioctl.h>
#endif

static enum ih_category cur_category;

static int h_compare_category_name(const void *vtype1, const void *vtype2)
{
	const int *type1 = vtype1;
	const int *type2 = vtype2;
	const char *name1 = genimg_get_cat_short_name(cur_category, *type1);
	const char *name2 = genimg_get_cat_short_name(cur_category, *type2);

	return strcmp(name1, name2);
}

static int show_valid_options(enum ih_category category)
{
	int *order;
	int count;
	int item;
	int i;

	count = genimg_get_cat_count(category);
	order = calloc(count, sizeof(*order));
	if (!order)
		return -ENOMEM;

	/* Sort the names in order of short name for easier reading */
	for (i = 0, item = 0; i < count; i++, item++) {
		while (!genimg_cat_has_id(category, item) && i < count) {
			item++;
			count--;
		}
		order[i] = item;
	}
	cur_category = category;
	qsort(order, count, sizeof(int), h_compare_category_name);

	fprintf(stderr, "\nInvalid %s, supported are:\n",
		genimg_get_cat_desc(category));
	for (i = 0; i < count; i++) {
		item = order[i];
		fprintf(stderr, "\t%-15s  %s\n",
			genimg_get_cat_short_name(category, item),
			genimg_get_cat_name(category, item));
	}
	fprintf(stderr, "\n");
	free(order);

	return 0;
}

/**
 * usage() - Print a usage message
 *
 * Return:
 *	EXIT_FAILURE (always)
 */
static int usage(const struct imgtool *itl, const char *msg)
{
	fprintf(stderr, "Error: %s\n", msg);
	fprintf(stderr, "Usage: %s [-T type] -l image\n"
			 "          -l ==> list image header information\n"
			 "          -T ==> parse image file as 'type'\n"
			 "          -q ==> quiet\n",
		itl->cmdname);
	fprintf(stderr,
		"       %s [-x] -A arch -O os -T type -C comp -a addr -e ep -n name -d data_file[:data_file...] image\n"
		"          -A ==> set architecture to 'arch'\n"
		"          -O ==> set operating system to 'os'\n"
		"          -T ==> set image type to 'type'\n"
		"          -C ==> set compression type 'comp'\n"
		"          -a ==> set load address to 'addr' (hex)\n"
		"          -e ==> set entry point to 'ep' (hex)\n"
		"          -n ==> set image name to 'name'\n"
		"          -R ==> set second image name to 'name'\n"
		"          -d ==> use image data from 'datafile'\n"
		"          -x ==> set XIP (execute in place)\n"
		"          -s ==> create an image with no data\n"
		"          -v ==> verbose\n",
		itl->cmdname);
	fprintf(stderr,
		"       %s [-D dtc_options] [-f fit-image.its|-f auto|-f auto-conf|-F] [-b <dtb> [-b <dtb>]] [-E] [-B size] [-i <ramdisk.cpio.gz>] fit-image\n"
		"           <dtb> file is used with -f auto, it may occur multiple times.\n",
		itl->cmdname);
	fprintf(stderr,
		"          -D => set all options for device tree compiler\n"
		"          -f => input filename for FIT source\n"
		"          -i => input filename for ramdisk file\n"
		"          -E => place data outside of the FIT structure\n"
		"          -B => align size in hex for FIT structure and header\n"
		"          -b => append the device tree binary to the FIT\n"
		"          -t => update the timestamp in the FIT\n");
#if IS_ENABLED(CONFIG_FIT_SIGNATURE)
	fprintf(stderr,
		"Signing / verified boot options: [-k keydir] [-K dtb] [ -c <comment>] [-p addr] [-r] [-N engine]\n"
		"          -k => set directory containing private keys\n"
		"          -K => write public keys to this .dtb file\n"
		"          -g => set key name hint\n"
		"          -G => use this signing key (in lieu of -k)\n"
		"          -c => add comment in signature node\n"
		"          -F => re-sign existing FIT image\n"
		"          -p => place external data at a static position\n"
		"          -r => mark keys used as 'required' in dtb\n"
		"          -N => openssl engine to use for signing\n"
		"          -o => algorithm to use for signing\n");
#else
	fprintf(stderr,
		"Signing / verified boot not supported (CONFIG_TOOLS_FIT_SIGNATURE undefined)\n");
#endif

	fprintf(stderr, "       %s -V ==> print version information and exit\n",
		itl->cmdname);
	fprintf(stderr, "Use '-T list' to see a list of available image types\n");
	fprintf(stderr, "Long options are available; read the man page for details\n");

	return EXIT_FAILURE;
}

static int add_content(struct imgtool *itl, int type, const char *fname)
{
	struct content_info *cont;

	cont = calloc(1, sizeof(*cont));
	if (!cont)
		return -1;
	cont->type = type;
	cont->fname = fname;
	if (itl->content_tail)
		itl->content_tail->next = cont;
	else
		itl->content_head = cont;
	itl->content_tail = cont;

	return 0;
}

static const char optstring[] =
	"a:A:b:B:c:C:d:D:e:Ef:Fg:G:i:k:K:ln:N:o:O:p:qrR:stT:vVx";

enum {
	OPT_LOAD_ONLY	= 1,
};

static const struct option longopts[] = {
	{ "load-address", required_argument, NULL, 'a' },
	{ "architecture", required_argument, NULL, 'A' },
	{ "device-tree", required_argument, NULL, 'b' },
	{ "alignment", required_argument, NULL, 'B' },
	{ "comment", required_argument, NULL, 'c' },
	{ "compression", required_argument, NULL, 'C' },
	{ "image", required_argument, NULL, 'd' },
	{ "dtcopts", required_argument, NULL, 'D' },
	{ "entry-point", required_argument, NULL, 'e' },
	{ "external", no_argument, NULL, 'E' },
	{ "fit", required_argument, NULL, 'f' },
	{ "update", no_argument, NULL, 'F' },
	{ "key-name-hint", required_argument, NULL, 'g' },
	{ "key-file", required_argument, NULL, 'G' },
	{ "help", no_argument, NULL, 'h' },
	{ "initramfs", required_argument, NULL, 'i' },
	{ "key-dir", required_argument, NULL, 'k' },
	{ "key-dest", required_argument, NULL, 'K' },
	{ "list", no_argument, NULL, 'l' },
	{ "config", required_argument, NULL, 'n' },
	{ "engine", required_argument, NULL, 'N' },
	{ "algo", required_argument, NULL, 'o' },
	{ "os", required_argument, NULL, 'O' },
	{ "position", required_argument, NULL, 'p' },
	{ "quiet", no_argument, NULL, 'q' },
	{ "key-required", no_argument, NULL, 'r' },
	{ "secondary-config", required_argument, NULL, 'R' },
	{ "no-copy", no_argument, NULL, 's' },
	{ "touch", no_argument, NULL, 't' },
	{ "type", required_argument, NULL, 'T' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "version", no_argument, NULL, 'V' },
	{ "xip", no_argument, NULL, 'x' },
	{ "load-only", no_argument, NULL, OPT_LOAD_ONLY },
	{ /* sentinel */ },
};

static int process_args(struct imgtool *itl, int argc, char **argv)
{
	char *ptr;
	int type = IH_TYPE_INVALID;
	char *datafile = NULL;
	int opt;

	while ((opt = getopt_long(argc, argv, optstring,
				  longopts, NULL)) != -1) {
		switch (opt) {
		case 'a':
			itl->addr = strtoull(optarg, &ptr, 16);
			if (*ptr) {
				fprintf(stderr, "%s: invalid load address %s\n",
					itl->cmdname, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'A':
			itl->arch = genimg_get_arch_id(optarg);
			if (itl->arch < 0) {
				show_valid_options(IH_ARCH);
				return usage(itl, "Invalid architecture");
			}
			itl->Aflag = 1;
			break;
		case 'b':
			if (add_content(itl, IH_TYPE_FLATDT, optarg)) {
				fprintf(stderr,
					"%s: Out of memory adding content '%s'",
					itl->cmdname, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'B':
			itl->bl_len = strtoull(optarg, &ptr, 16);
			if (*ptr) {
				fprintf(stderr, "%s: invalid block length %s\n",
					itl->cmdname, optarg);
				return EXIT_FAILURE;
			}

			break;
		case 'c':
			itl->comment = optarg;
			break;
		case 'C':
			itl->comp = genimg_get_comp_id(optarg);
			if (itl->comp < 0) {
				show_valid_options(IH_COMP);
				return usage(itl, "Invalid compression type");
			}
			break;
		case 'd':
			itl->datafile = optarg;
			itl->dflag = 1;
			break;
		case 'D':
			itl->dtc = optarg;
			break;
		case 'e':
			itl->ep = strtoull(optarg, &ptr, 16);
			if (*ptr) {
				fprintf(stderr, "%s: invalid entry point %s\n",
					itl->cmdname, optarg);
				return EXIT_FAILURE;
			}
			itl->eflag = 1;
			break;
		case 'E':
			itl->external_data = true;
			break;
		case 'f':
			datafile = optarg;
			if (!strcmp(datafile, "auto"))
				itl->auto_fit = AF_HASHED_IMG;
			else if (!strcmp(datafile, "auto-conf"))
				itl->auto_fit = AF_SIGNED_CONF;
			/* fallthrough */
		case 'F':
			/*
			 * The flattened image tree (FIT) format
			 * requires a flattened device tree image type
			 */
			itl->type = IH_TYPE_FLATDT;
			itl->fflag = 1;
			break;
		case 'g':
			itl->keyname = optarg;
			break;
		case 'G':
			itl->keyfile = optarg;
			break;
		case 'i':
			itl->fit_ramdisk = optarg;
			break;
		case 'k':
			itl->keydir = optarg;
			break;
		case 'K':
			itl->keydest = optarg;
			break;
		case 'l':
			itl->lflag = 1;
			break;
		case 'n':
			itl->imagename = optarg;
			break;
		case 'N':
			itl->engine_id = optarg;
			break;
		case 'o':
			itl->algo_name = optarg;
			break;
		case 'O':
			itl->os = genimg_get_os_id(optarg);
			if (itl->os < 0) {
				show_valid_options(IH_OS);
				return usage(itl, "Invalid operating system");
			}
			break;
		case 'p':
			itl->external_offset = strtoull(optarg, &ptr, 16);
			if (*ptr) {
				fprintf(stderr, "%s: invalid offset size %s\n",
					itl->cmdname, optarg);
				return EXIT_FAILURE;
			}
			break;
		case 'q':
			itl->quiet = 1;
			break;
		case 'r':
			itl->require_keys = 1;
			break;
		case 'R':
			/*
			 * This entry is for the second configuration
			 * file, if only one is not enough.
			 */
			itl->imagename2 = optarg;
			break;
		case 's':
			itl->skipcpy = 1;
			break;
		case 't':
			itl->reset_timestamp = 1;
			break;
		case 'T':
			if (strcmp(optarg, "list") == 0) {
				show_valid_options(IH_TYPE);
				exit(EXIT_SUCCESS);
			}
			type = genimg_get_type_id(optarg);
			if (type < 0) {
				show_valid_options(IH_TYPE);
				return usage(itl, "Invalid image type");
			}
			break;
		case 'v':
			itl->vflag++;
			break;
		case 'V':
			printf("mkimage version %s\n", PLAIN_VERSION);
			exit(EXIT_SUCCESS);
		case 'x':
			itl->xflag++;
			break;
		case OPT_LOAD_ONLY:
			itl->load_only = true;
			break;
		default:
			return usage(itl, "Invalid option");
		}
	}

	/* The last parameter is expected to be the imagefile */
	if (optind < argc)
		itl->imagefile = argv[optind];

	if (itl->auto_fit == AF_SIGNED_CONF) {
		if (!itl->keyname || !itl->algo_name)
			return usage(itl,
				     "Missing key/algo for auto-FIT with signed configs (use -g -o)");
	} else if (itl->auto_fit == AF_HASHED_IMG && itl->keyname) {
		itl->auto_fit = AF_SIGNED_IMG;
		if (!itl->algo_name)
			return usage(itl,
				"Missing algorithm for auto-FIT with signed images (use -g)");
	}
	if (itl->auto_fit)
		itl->reset_timestamp = 1;

	/*
	 * For auto-generated FIT images we need to know the image type to put
	 * in the FIT, which is separate from the file's image type (which
	 * will always be IH_TYPE_FLATDT in this case).
	 */
	if (itl->type == IH_TYPE_FLATDT) {
		itl->fit_image_type = type ? type : IH_TYPE_KERNEL;
		/* For auto-FIT, datafile has to be provided with -d */
		if (!itl->auto_fit)
			itl->datafile = datafile;
		else if (!itl->datafile && type != IH_TYPE_FLATDT)
			return usage(itl,
				     "Missing data file for auto-FIT (use -d)");
	} else if (itl->lflag || type != IH_TYPE_INVALID) {
		if (type == IH_TYPE_SCRIPT && !itl->datafile)
			return usage(itl,
				     "Missing data file for script (use -d)");
		itl->type = type;
	}

	if (!itl->imagefile)
		return usage(itl, "Missing output filename");

	return 0;
}

static int verify_image(struct imgtool *itl, const struct imgtool_funcs *tparams)
{
	struct stat sbuf;
	void *ptr;
	int ifd;

	ifd = open(itl->imagefile, O_RDONLY | O_BINARY);
	if (ifd < 0) {
		fprintf(stderr, "%s: Can't open %s: %s\n",
			itl->cmdname, itl->imagefile,
			strerror(errno));
		return EXIT_FAILURE;
	}

	if (fstat(ifd, &sbuf) < 0) {
		fprintf(stderr, "%s: Can't stat %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}
	itl->file_size = sbuf.st_size;

	ptr = mmap(0, itl->file_size, PROT_READ, MAP_SHARED, ifd, 0);
	if (ptr == MAP_FAILED) {
		fprintf(stderr, "%s: Can't map %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (tparams->verify_header((unsigned char *)ptr, itl->file_size, itl) != 0) {
		fprintf(stderr, "%s: Failed to verify header of %s\n",
			itl->cmdname, itl->imagefile);
		return EXIT_FAILURE;
	}

	(void)munmap(ptr, itl->file_size);
	(void)close(ifd);

	return 0;
}

static int copy_file(struct imgtool *itl, int ifd, const char *datafile, int pad)
{
	int dfd;
	struct stat sbuf;
	unsigned char *ptr;
	int tail;
	int zero = 0;
	uint8_t zeros[4096];
	int offset = 0;
	int size, ret;
	struct imgtool_funcs *tparams = imagetool_get_type(itl->type);

	memset(zeros, 0, sizeof(zeros));

	if (itl->vflag)
		fprintf(stderr, "Adding Image %s\n", datafile);

	dfd = open(datafile, O_RDONLY | O_BINARY);
	if (dfd < 0) {
		fprintf(stderr, "%s: Can't open %s: %s\n",
			itl->cmdname, datafile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (fstat(dfd, &sbuf) < 0) {
		fprintf(stderr, "%s: Can't stat %s: %s\n",
			itl->cmdname, datafile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (sbuf.st_size == 0) {
		fprintf(stderr, "%s: Input file %s is empty, bailing out\n",
			itl->cmdname, datafile);
		return EXIT_FAILURE;
	}

	ptr = mmap(0, sbuf.st_size, PROT_READ, MAP_SHARED, dfd, 0);
	if (ptr == MAP_FAILED) {
		fprintf(stderr, "%s: Can't read %s: %s\n",
			itl->cmdname, datafile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (itl->xflag &&
	    ((itl->type > IH_TYPE_INVALID && itl->type < IH_TYPE_FLATDT) ||
	     itl->type == IH_TYPE_KERNEL_NOLOAD ||
	     itl->type == IH_TYPE_FIRMWARE_IVT)) {
		unsigned char *p = NULL;
		/*
		 * XIP: do not append the struct legacy_img_hdr at the
		 * beginning of the file, but consume the space
		 * reserved for it.
		 */

		if ((unsigned int)sbuf.st_size < tparams->header_size) {
			fprintf(stderr,
				"%s: Bad size: \"%s\" is too small for XIP\n",
				itl->cmdname, datafile);
			return EXIT_FAILURE;
		}

		for (p = ptr; p < ptr + tparams->header_size; p++) {
			if (*p != 0xff) {
				fprintf(stderr,
					"%s: Bad file: \"%s\" has invalid buffer for XIP\n",
					itl->cmdname, datafile);
				return EXIT_FAILURE;
			}
		}

		offset = tparams->header_size;
	}

	size = sbuf.st_size - offset;

	ret = write(ifd, ptr + offset, size);
	if (ret != size) {
		if (ret < 0)
			fprintf(stderr, "%s: Write error on %s: %s\n",
				itl->cmdname, itl->imagefile, strerror(errno));
		else if (ret < size)
			fprintf(stderr, "%s: Write only %d/%d bytes, "
				"probably no space left on the device\n",
				itl->cmdname, ret, size);
		return EXIT_FAILURE;
	}

	tail = size % 4;
	if (pad == 1 && tail) {
		if (write(ifd, (char *)&zero, 4 - tail) != 4 - tail) {
			fprintf(stderr, "%s: Write error on %s: %s\n",
				itl->cmdname, itl->imagefile,
				strerror(errno));
			return EXIT_FAILURE;
		}
	} else if (pad > 1) {
		while (pad > 0) {
			int todo = sizeof(zeros);

			if (todo > pad)
				todo = pad;
			if (write(ifd, (char *)&zeros, todo) != todo) {
				fprintf(stderr, "%s: Write error on %s: %s\n",
					itl->cmdname, itl->imagefile,
					strerror(errno));
				return EXIT_FAILURE;
			}
			pad -= todo;
		}
	}

	(void)munmap((void *)ptr, sbuf.st_size);
	(void)close(dfd);

	return 0;
}

static int copy_datafile(struct imgtool *itl, int ifd, char *file)
{
	if (!file)
		return 0;
	for (;;) {
		char *sep = strchr(file, ':');

		if (sep) {
			*sep = '\0';
			if (copy_file(itl, ifd, file, 1))
				return EXIT_FAILURE;
			*sep++ = ':';
			file = sep;
		} else {
			if (copy_file(itl, ifd, file, 0))
				return EXIT_FAILURE;
			break;
		}
	}

	return 0;
}

/**
 * check_params() - Do some initial checks on the parameters
 *
 * @itl: Image-tool info
 * @tfuncs: image-tool functions
 * Return: 0 if OK, non-zero on error
 */
static int check_params(struct imgtool *itl, struct imgtool_funcs *tfuncs)
{
	/* set tparams as per input type_id */
	tfuncs = imagetool_get_type(itl->type);
	if (!tfuncs && !itl->lflag) {
		fprintf (stderr, "%s: unsupported type %s\n",
			itl->cmdname, genimg_get_type_name(itl->type));
		return -EINVAL;
	}

	/*
	 * check the passed arguments parameters meets the requirements
	 * as per image type to be generated/listed
	 */
	if (tfuncs && tfuncs->check_params)
		if (tfuncs->check_params(itl))
			return usage(itl, "Bad parameters for image type");

	if (!itl->eflag) {
		itl->ep = itl->addr;
		/* If XIP, entry point must be after the U-Boot header */
		if (itl->xflag && tfuncs)
			itl->ep += tfuncs->header_size;
	}

	return 0;
}

static int process_fit(struct imgtool *itl, struct imgtool_funcs *tfuncs)
{
	int retval;

	if (!tfuncs) {
		fprintf(stderr, "%s: Missing FIT support\n",
			itl->cmdname);
		return -ENOTSUP;
	}

	/*
	 * in some cases, some additional processing needs to be done if fflag
	 * is defined
	 *
	 * E.g. fit_handle_file for Fit file support
	 */
	if (tfuncs->fflag_handle) {
		retval = tfuncs->fflag_handle(itl);
		if (retval != EXIT_SUCCESS) {
			if (retval == FDT_ERR_NOTFOUND) {
				// Already printed error, exit cleanly
				exit(EXIT_FAILURE);
			}
			return usage(itl, "Bad parameters for FIT image type");
		}
	}

	return 0;
}

/**
 * open_image() - Open the image file to create/update
 *
 * @itl: Image-tool info
 *
 * Return: file handle if OK, or -ve on error
 */
static int open_image(struct imgtool *itl)
{
	int ifd;

	if (itl->lflag || itl->fflag) {
		ifd = open(itl->imagefile, O_RDONLY | O_BINARY);
	} else {
		ifd = open(itl->imagefile,
			   O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0666);
	}

	if (ifd < 0) {
		fprintf(stderr, "%s: Can't open %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return -ENOENT;
	}

	return ifd;
}

/**
 * list_or_process() - List the image, or do processing on it
 *
 * @itl: Image-tool info
 * @tfuncs: image-tool functions
 * @ifd: File handle of file to process
 *
 * Return: file handle if OK, or -ve on error
 */
static int list_or_process(struct imgtool *itl, struct imgtool_funcs *tfuncs,
			   int ifd)
{
	struct stat sbuf;
	uint64_t size;
	void *ptr;
	int ret;

	if (fstat(ifd, &sbuf) < 0) {
		fprintf(stderr, "%s: Can't stat %s: %s\n", itl->cmdname,
			itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	if ((sbuf.st_mode & S_IFMT) == S_IFBLK) {
#ifdef __linux__
#if defined(__linux__) && defined(_IOR) && !defined(BLKGETSIZE64)
#define BLKGETSIZE64 _IOR(0x12, 114, size_t)	/* return device size in bytes (u64 *arg) */
#endif
		if (ioctl(ifd, BLKGETSIZE64, &size) < 0) {
			fprintf(stderr,
				"%s: failed to get size of block device \"%s\"\n",
				itl->cmdname, itl->imagefile);
			return EXIT_FAILURE;
		}
#else
		fprintf(stderr,
			"%s: \"%s\" is block device, don't know how to get its size\n",
			itl->cmdname, itl->imagefile);
		return EXIT_FAILURE;
#endif
	} else if (tfuncs && sbuf.st_size < (off_t)tfuncs->header_size) {
		fprintf(stderr,
			"%s: Bad size: \"%s\" is not valid image: size %llu < %u\n",
			itl->cmdname, itl->imagefile,
			(unsigned long long)sbuf.st_size,
			tfuncs->header_size);
		return EXIT_FAILURE;
		size = sbuf.st_size;
	} else {
		size = sbuf.st_size;
	}

	ptr = mmap(0, size, PROT_READ, MAP_SHARED, ifd, 0);
	if (ptr == MAP_FAILED) {
		fprintf(stderr, "%s: Can't read %s: %s\n", itl->cmdname,
			itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	/*
	 * Verify the header format based on the expected header for image
	 * type in titl-> If tfuncs is NULL simply check all image types
	 * to find one that matches our header.
	 */
	ret = imagetool_verify_print_header(ptr, &sbuf, tfuncs, itl);

	(void)munmap((void *)ptr, sbuf.st_size);
	(void)close(ifd);
	if (!ret)
		summary_show(&itl->summary, itl->imagefile, itl->keydest);

	return ret;
}

/**
 * run_mkimage() - Run the mkimage tool
 *
 * The program arguments are in params
 *
 * Return: 0 on success, or non-zero exit code
 */
static int run_mkimage(struct imgtool *itl)
{
	struct imgtool_funcs *tparams = NULL;
	struct stat sbuf;
	int pad_len = 0;
	size_t map_len;
	int ifd = -1;
	char *ptr;
	int dfd;

	tparams = imagetool_get_type(itl->type);
	if (check_params(itl, tparams))
		return EXIT_FAILURE;
	if (itl->fflag && process_fit(itl, tparams))
		return EXIT_FAILURE;

	ifd = open_image(itl);
	if (ifd < 0)
		return EXIT_FAILURE;

	if (itl->lflag || itl->fflag)
		return list_or_process(itl, tparams, ifd);

	if (!itl->skipcpy && itl->type != IH_TYPE_MULTI && itl->type != IH_TYPE_SCRIPT) {
		if (!itl->datafile) {
			fprintf(stderr, "%s: Option -d with image data file was not specified\n",
				itl->cmdname);
			return EXIT_FAILURE;
		}
		dfd = open(itl->datafile, O_RDONLY | O_BINARY);
		if (dfd < 0) {
			fprintf(stderr, "%s: Can't open %s: %s\n",
				itl->cmdname, itl->datafile,
				strerror(errno));
			return EXIT_FAILURE;
		}

		if (fstat(dfd, &sbuf) < 0) {
			fprintf(stderr, "%s: Can't stat %s: %s\n",
				itl->cmdname, itl->datafile,
				strerror(errno));
			return EXIT_FAILURE;
		}

		itl->file_size = sbuf.st_size + tparams->header_size;
		close(dfd);
	}

	/*
	 * In case there an header with a variable
	 * length will be added, the corresponding
	 * function is called. This is responsible to
	 * allocate memory for the header itself.
	 */
	if (tparams->vrec_header)
		pad_len = tparams->vrec_header(itl, tparams);
	else
		memset(tparams->hdr, 0, tparams->header_size);

	if (write(ifd, tparams->hdr, tparams->header_size)
					!= tparams->header_size) {
		fprintf (stderr, "%s: Write error on %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (!itl->skipcpy) {
		if (itl->type == IH_TYPE_MULTI ||
		    itl->type == IH_TYPE_SCRIPT) {
			char *file = itl->datafile;
			uint32_t size;

			for (;;) {
				char *sep = NULL;

				if (file) {
					if ((sep = strchr(file, ':')) != NULL) {
						*sep = '\0';
					}

					if (stat (file, &sbuf) < 0) {
						fprintf (stderr, "%s: Can't stat %s: %s\n",
							 itl->cmdname, file, strerror(errno));
						return EXIT_FAILURE;
					}
					size = cpu_to_uimage (sbuf.st_size);
				} else {
					size = 0;
				}

				if (write(ifd, (char *)&size, sizeof(size)) != sizeof(size)) {
					fprintf (stderr, "%s: Write error on %s: %s\n",
						 itl->cmdname, itl->imagefile,
						 strerror(errno));
					return EXIT_FAILURE;
				}

				if (!file) {
					break;
				}

				if (sep) {
					*sep = ':';
					file = sep + 1;
				} else {
					file = NULL;
				}
			}
			if (copy_datafile(itl, ifd, itl->datafile))
				return EXIT_FAILURE;
		} else if (itl->type == IH_TYPE_PBLIMAGE) {
			/* PBL has special Image format, implements its' own */
			pbl_load_uboot(ifd, itl);
		} else if (itl->type == IH_TYPE_ZYNQMPBIF) {
			/* Image file is meta, walk through actual targets */
			int ret;

			ret = zynqmpbif_copy_image(ifd, itl);
			if (ret)
				return ret;
		} else if (itl->type == IH_TYPE_IMX8IMAGE) {
			/* i.MX8/8X has special Image format */
			int ret;

			ret = imx8image_copy_image(ifd, itl);
			if (ret)
				return ret;
		} else if (itl->type == IH_TYPE_IMX8MIMAGE) {
			/* i.MX8M has special Image format */
			int ret;

			ret = imx8mimage_copy_image(ifd, itl);
			if (ret)
				return ret;
		} else if ((itl->type == IH_TYPE_RKSD) ||
				(itl->type == IH_TYPE_RKSPI)) {
			/* Rockchip has special Image format */
			int ret;

			ret = rockchip_copy_image(ifd, itl);
			if (ret)
				return ret;
		} else {
			if (copy_file(itl, ifd, itl->datafile, pad_len))
				return EXIT_FAILURE;
		}
		if (itl->type == IH_TYPE_FIRMWARE_IVT) {
			/* Add alignment and IVT */
			uint32_t aligned_filesize = ALIGN(itl->file_size,
							  0x1000);
			flash_header_v2_t ivt_header = { { 0xd1, 0x2000, 0x40 },
					itl->addr, 0, 0, 0, itl->addr
							+ aligned_filesize
							- tparams->header_size,
					itl->addr + aligned_filesize
							- tparams->header_size
							+ 0x20, 0 };
			int i = itl->file_size;
			for (; i < aligned_filesize; i++) {
				if (write(ifd, (char *) &i, 1) != 1) {
					fprintf(stderr,
							"%s: Write error on %s: %s\n",
							itl->cmdname,
							itl->imagefile,
							strerror(errno));
					return EXIT_FAILURE;
				}
			}
			if (write(ifd, &ivt_header, sizeof(flash_header_v2_t))
					!= sizeof(flash_header_v2_t)) {
				fprintf(stderr, "%s: Write error on %s: %s\n",
						itl->cmdname,
						itl->imagefile,
						strerror(errno));
				return EXIT_FAILURE;
			}
		}
	}

	/* We're a bit of paranoid */
#if defined(_POSIX_SYNCHRONIZED_IO) && \
   !defined(__sun__) && \
   !defined(__FreeBSD__) && \
   !defined(__OpenBSD__) && \
   !defined(__APPLE__)
	(void) fdatasync (ifd);
#else
	(void) fsync (ifd);
#endif

	if (fstat(ifd, &sbuf) < 0) {
		fprintf (stderr, "%s: Can't stat %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}
	itl->file_size = sbuf.st_size;

	map_len = sbuf.st_size;
	ptr = mmap(0, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, ifd, 0);
	if (ptr == MAP_FAILED) {
		fprintf (stderr, "%s: Can't map %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Setup the image header as per input image type*/
	if (tparams->set_header)
		tparams->set_header(ptr, &sbuf, ifd, itl);
	else {
		fprintf (stderr, "%s: Can't set header for %s\n",
			itl->cmdname, tparams->name);
		return EXIT_FAILURE;
	}

	/* Print the image information by processing image header */
	if (tparams->print_header)
		tparams->print_header(ptr, itl);
	else {
		fprintf (stderr, "%s: Can't print header for %s\n",
			itl->cmdname, tparams->name);
	}

	(void)munmap((void *)ptr, map_len);

	/* We're a bit of paranoid */
#if defined(_POSIX_SYNCHRONIZED_IO) && \
   !defined(__sun__) && \
   !defined(__FreeBSD__) && \
   !defined(__OpenBSD__) && \
   !defined(__APPLE__)
	(void) fdatasync (ifd);
#else
	(void) fsync (ifd);
#endif

	if (close(ifd)) {
		fprintf (stderr, "%s: Write error on %s: %s\n",
			itl->cmdname, itl->imagefile, strerror(errno));
		return EXIT_FAILURE;
	}

	if (tparams->verify_header && verify_image(itl, tparams))
		return EXIT_FAILURE;

	return 0;
}

int main(int argc, char **argv)
{
	/* parameters initialized by core will be used by the image type code */
	struct imgtool itl = {
		.cmdname = *argv,
		.os = IH_OS_LINUX,
		.arch = IH_ARCH_PPC,
		.type = IH_TYPE_KERNEL,
		.comp = IH_COMP_GZIP,
		.dtc = MKIMAGE_DEFAULT_DTC_OPTIONS,
		.imagename = "",
		.imagename2 = "",
	};
	int ret;

	ret = process_args(&itl, argc, argv);
	if (!ret)
		ret = run_mkimage(&itl);

	return ret;
}
