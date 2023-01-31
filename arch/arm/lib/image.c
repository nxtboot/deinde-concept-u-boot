// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2000-2009
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 */

#include <dm.h>
#include <env.h>
#include <image.h>
#include <lmb.h>
#include <mapmem.h>
#include <rng.h>
#include <asm/global_data.h>
#include <linux/bitops.h>
#include <linux/sizes.h>

DECLARE_GLOBAL_DATA_PTR;

#define LINUX_ARM64_IMAGE_MAGIC 0x644d5241

/* See Documentation/arm64/booting.txt in the Linux kernel */
struct Image_header {
	uint32_t	code0;		/* Executable code */
	uint32_t	code1;		/* Executable code */
	uint64_t	text_offset;	/* Image load offset, LE */
	uint64_t	image_size;	/* Effective Image size, LE */
	uint64_t	flags;		/* Kernel flags, LE */
	uint64_t	res2;		/* reserved */
	uint64_t	res3;		/* reserved */
	uint64_t	res4;		/* reserved */
	uint32_t	magic;		/* Magic number */
	uint32_t	res5;
};

bool booti_is_valid(const void *img)
{
	const struct Image_header *ih = img;

	return ih->magic == le32_to_cpu(LINUX_ARM64_IMAGE_MAGIC);
}

/**
 * booti_parse() - Read the placement fields from an Image header
 *
 * @ih: Image header
 * @text_offsetp: Returns the offset of the Image from its 2MB-aligned base
 * @image_sizep: Returns the size of the Image in memory, including .bss
 * @flagsp: Returns the kernel flags
 */
static void booti_parse(const struct Image_header *ih, u64 *text_offsetp,
			u64 *image_sizep, u64 *flagsp)
{
	/*
	 * Prior to Linux commit a2c1d73b94ed, the text_offset field
	 * is of unknown endianness.  In these cases, the image_size
	 * field is zero, and we can assume a fixed value of 0x80000.
	 */
	if (ih->image_size == 0) {
		puts("Image lacks image_size field, assuming 16MiB\n");
		*image_sizep = 16 << 20;
		*text_offsetp = 0x80000;
	} else {
		*image_sizep = le64_to_cpu(ih->image_size);
		*text_offsetp = le64_to_cpu(ih->text_offset);
	}
	*flagsp = le64_to_cpu(ih->flags);
}

/**
 * bootargs_has_nokaslr() - Check whether the kernel command line has nokaslr
 *
 * Linux uses this to disable KASLR, including the physical randomisation done
 * by its EFI stub, so honour it here too.
 *
 * Return: true if the bootargs environment variable contains 'nokaslr'
 */
static bool bootargs_has_nokaslr(void)
{
	const char *s = env_get("bootargs");

	while (s && *s) {
		const char *end;

		while (*s == ' ')
			s++;
		end = strchrnul(s, ' ');
		if (end - s == strlen("nokaslr") &&
		    !strncmp(s, "nokaslr", end - s))
			return true;
		s = end;
	}

	return false;
}

/**
 * booti_random_base() - Choose a random base for the Image
 *
 * @image_size: Size of the Image in memory
 * @basep: Returns the 2MB-aligned base, reserved in lmb
 * Return: 0 if OK, -ve if no RNG is available or no space was found
 */
static int booti_random_base(u64 image_size, u64 *basep)
{
	struct udevice *dev;
	phys_addr_t base;
	ulong rnd;
	int ret;

	ret = uclass_get_device(UCLASS_RNG, 0, &dev);
	if (!ret)
		ret = dm_rng_read(dev, &rnd, sizeof(rnd));
	if (ret) {
		printf("No RNG (err=%d), so not randomising Image placement\n",
		       ret);
		return ret;
	}
	ret = lmb_alloc_random(image_size, SZ_2M, LMB_NONE, rnd, &base);
	if (ret)
		return ret;
	*basep = base;

	return 0;
}

/**
 * booti_check_granule() - Check that the CPU supports the kernel's page size
 *
 * The Image header's flags say which page size the kernel was built for. A
 * kernel started on a CPU which lacks that translation granule hangs silently
 * in its own early check, so refuse it here with an explanation, as the
 * kernel's EFI stub does.
 *
 * @flags: Kernel flags from the header
 * Return: 0 if the page size is supported or not specified, -EOPNOTSUPP if not
 */
static int booti_check_granule(u64 flags)
{
	static const char *const name[] = { NULL, "4KB", "16KB", "64KB" };
	int page = (flags >> 1) & 3;
	u64 mmfr0;
	bool ok;

	if (!page)
		return 0;

	asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r" (mmfr0));
	switch (page) {
	case 1:		/* TGran4: 0xf means not supported */
		ok = ((mmfr0 >> 28) & 0xf) != 0xf;
		break;
	case 2:		/* TGran16: 0 means not supported */
		ok = ((mmfr0 >> 20) & 0xf) != 0;
		break;
	default:	/* TGran64: 0xf means not supported */
		ok = ((mmfr0 >> 24) & 0xf) != 0xf;
		break;
	}
	if (!ok) {
		printf("This %s-page kernel is not supported by the CPU\n",
		       name[page]);
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * booti_place() - Decide where an Image should go
 *
 * @cur: Current address of the Image, or 0 if it is not in memory yet
 * @text_offset: Offset of the Image from its 2MB-aligned base
 * @image_size: Size of the Image in memory
 * @flags: Kernel flags from the header
 * @force_reloc: Place the Image at the start of RAM regardless
 * @placed: true if booti_alloc() already chose @cur, so it should not be
 *	randomised again
 * @addrp: Returns the address for the Image, i.e. its base plus text_offset
 * Return: 0 if OK, -ENOSPC if there was not enough lmb space
 */
static int booti_place(ulong cur, u64 text_offset, u64 image_size, u64 flags,
		       bool force_reloc, bool placed, ulong *addrp)
{
	u64 dst;

	/*
	 * If bit 3 of the flags field is set, the 2MB aligned base of the
	 * kernel image can be anywhere in physical memory, so respect
	 * images->ep.  Otherwise, relocate the image to the base of RAM
	 * since memory below it is not accessible via the linear mapping.
	 */
	if (!force_reloc && (flags & BIT(3))) {
		if (IS_ENABLED(CONFIG_LMB)) {
			bool done = false;

			/*
			 * Choose a random base unless booti_alloc() already
			 * did, then leave the Image where it is, if that will
			 * do, else find somewhere for it
			 */
			if (!placed && IS_ENABLED(CONFIG_BOOTI_RANDOMIZE_BASE) &&
			    !bootargs_has_nokaslr() &&
			    !booti_random_base(image_size, &dst))
				done = true;
			if (!done && cur >= text_offset &&
			    IS_ALIGNED(cur - text_offset, SZ_2M) &&
			    !lmb_alloc_addr(cur - text_offset, image_size,
					    LMB_NONE)) {
				dst = cur - text_offset;
				done = true;
			}
			if (!done) {
				dst = lmb_alloc(image_size, SZ_2M);
				if (!dst)
					return -ENOSPC;
			}
		} else {
			dst = cur - text_offset;
		}
	} else {
		dst = gd->dram[0].start;
	}

	*addrp = ALIGN(dst, SZ_2M) + text_offset;

	return 0;
}

/**
 * setup_image() - Check an Image and decide where it should go
 *
 * @image: Address of the Image
 * @relocated_addr: Returns the address the Image should run from
 * @size: Returns the size of the Image in memory
 * @force_reloc: Place the Image at the start of RAM regardless
 * @placed: true if booti_alloc() chose @image
 * Return: 0 if OK, -ve on error
 */
static int setup_image(ulong image, ulong *relocated_addr, ulong *size,
		       bool force_reloc, bool placed)
{
	u64 image_size, text_offset, flags;
	struct Image_header *ih;
	int ret;

	*relocated_addr = image;

	ih = (struct Image_header *)map_sysmem(image, 0);

	if (!booti_is_valid(ih)) {
		puts("Bad Linux ARM64 Image magic!\n");
		return -EPERM;
	}
	booti_parse(ih, &text_offset, &image_size, &flags);
	unmap_sysmem(ih);
	*size = image_size;

	ret = booti_check_granule(flags);
	if (ret)
		return ret;

	return booti_place(image, text_offset, image_size, flags, force_reloc,
			   placed, relocated_addr);
}

int booti_setup(ulong image, ulong *relocated_addr, ulong *size,
		bool force_reloc)
{
	return setup_image(image, relocated_addr, size, force_reloc, false);
}

/**
 * alloc_size() - Work out how much space to reserve for an Image
 *
 * An Image's .bss follows its file contents, and its size is only known once
 * the header can be read, after decompression. Allow an eighth extra for it,
 * so that the Image can normally stay where it is once its true size is known
 *
 * @size: Size of the Image file, i.e. the decompressed size
 * Return: Size to reserve
 */
static ulong alloc_size(ulong size)
{
	return size + ALIGN(size / 8, SZ_2M);
}

int booti_alloc(ulong size, ulong *addrp)
{
	phys_addr_t addr;
	u64 base;

	if (!IS_ENABLED(CONFIG_LMB))
		return -ENOSYS;
	if (IS_ENABLED(CONFIG_BOOTI_RANDOMIZE_BASE) &&
	    !bootargs_has_nokaslr() &&
	    !booti_random_base(alloc_size(size), &base)) {
		*addrp = base;
		return 0;
	}
	addr = lmb_alloc(alloc_size(size), SZ_2M);
	if (!addr)
		return -ENOSPC;
	*addrp = addr;

	return 0;
}

int booti_check(ulong image, ulong size, ulong *relocated_addr, ulong *sizep)
{
	/*
	 * Release the space so that setup_image() can reserve exactly what the
	 * header says is needed, or move the Image if that is not possible
	 */
	lmb_free(image, alloc_size(size));

	return setup_image(image, relocated_addr, sizep, false, true);
}
