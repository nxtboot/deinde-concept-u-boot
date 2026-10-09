// SPDX-License-Identifier: BSD-2-Clause
/*
   Copyright (c) 2001 William L. Pitts
*/

#include <command.h>
#include <cpu_func.h>
#include <elf.h>
#include <env.h>
#include <errno.h>
#include <mapmem.h>
#include <net.h>
#include <vxworks.h>
#ifdef CONFIG_X86
#include <vesa.h>
#include <asm/e820.h>
#include <linux/linkage.h>
#endif

/**
 * bootelf_exec() - start the ELF image execution.
 *
 * @entry: address of entry point of ELF.
 *
 * May by used to allow ports to override the default behavior.
 */
unsigned long bootelf_exec(ulong (*entry)(int, char * const[]),
			   int argc, char *const argv[])
{
	return entry(argc, argv);
}

/**
 * bootelf() - Boot ELF from memory.
 *
 * @addr:  Loading address of ELF in memory.
 * @flags: Bits like ELF_PHDR to control boot details.
 * @argc: May be used to pass command line arguments (maybe unused).
 *	  Necessary for backward compatibility with the CLI command.
 *	  If unused, must be 0.
 * @argv: see @argc. If unused, must be NULL.
 * Return: Number returned by ELF application.
 *
 * Sets errno = ENOEXEC if the ELF image is not valid.
 */
unsigned long bootelf(unsigned long addr, Bootelf_flags flags,
		      int argc, char *const argv[])
{
	unsigned long entry_addr;
	char *args[] = {"", NULL};

	errno = 0;

	if (!valid_elf_image(addr)) {
		errno = ENOEXEC;
		return 1;
	}

	entry_addr = flags.phdr ? load_elf_image_phdr(addr)
					    : load_elf_image_shdr(addr);

	if (!flags.autostart)
		return 0;

	if (!argc && !argv) {
		argc = 1;
		argv = args;
	}

	return bootelf_exec((void *)entry_addr, argc, argv);
}

/*
 * A very simple ELF64 loader, assumes the image is valid, returns the
 * entry point address.
 *
 * Note if U-Boot is 32-bit, the loader assumes the to segment's
 * physical address and size is within the lower 32-bit address space.
 */
unsigned long load_elf64_image_phdr(unsigned long addr)
{
	Elf64_Ehdr *ehdr; /* Elf header structure pointer */
	Elf64_Phdr *phdr; /* Program header structure pointer */
	unsigned long entry;
	void *base;
	int i;

	/* the size is not known until the header is read, so map without it */
	base = map_sysmem(addr, 0);
	ehdr = base;
	phdr = base + (ulong)ehdr->e_phoff;

	/* Load each program header */
	for (i = 0; i < ehdr->e_phnum; ++i, ++phdr) {
		void *dst, *src;

		/* Only load PT_LOAD program header */
		if (phdr->p_type != PT_LOAD)
			continue;

		dst = map_sysmem(phdr->p_paddr, phdr->p_memsz);
		src = base + phdr->p_offset;

		debug("Loading phdr %i to 0x%p (%lu bytes)\n",
		      i, dst, (ulong)phdr->p_filesz);
		if (phdr->p_filesz)
			memcpy(dst, src, phdr->p_filesz);
		if (phdr->p_filesz != phdr->p_memsz)
			memset(dst + phdr->p_filesz, 0x00,
			       phdr->p_memsz - phdr->p_filesz);
		unmap_sysmem(dst);
		flush_cache(rounddown((ulong)phdr->p_paddr,
				      ARCH_DMA_MINALIGN),
			    roundup(phdr->p_memsz, ARCH_DMA_MINALIGN));
	}

	entry = ehdr->e_entry;
	if (ehdr->e_machine == EM_PPC64 && (ehdr->e_flags &
					    EF_PPC64_ELFV1_ABI)) {
		/*
		 * For the 64-bit PowerPC ELF V1 ABI, e_entry is a function
		 * descriptor pointer with the first double word being the
		 * address of the entry point of the function.
		 */
		Elf64_Addr *desc = map_sysmem(entry, sizeof(*desc));

		entry = *desc;
		unmap_sysmem(desc);
	}
	unmap_sysmem(base);

	return entry;
}

unsigned long load_elf64_image_shdr(unsigned long addr)
{
	Elf64_Ehdr *ehdr; /* Elf header structure pointer */
	Elf64_Shdr *shdr; /* Section header structure pointer */
	unsigned char *strtab = 0; /* String table pointer */
	unsigned char *image; /* Binary image pointer */
	unsigned long entry;
	void *base;
	int i; /* Loop counter */

	/* the size is not known until the header is read, so map without it */
	base = map_sysmem(addr, 0);
	ehdr = base;

	/* Find the section header string table for output info */
	shdr = base + (ulong)ehdr->e_shoff +
		(ehdr->e_shstrndx * sizeof(Elf64_Shdr));

	if (shdr->sh_type == SHT_STRTAB)
		strtab = base + (ulong)shdr->sh_offset;

	/* Load each appropriate section */
	for (i = 0; i < ehdr->e_shnum; ++i) {
		void *dst;

		shdr = base + (ulong)ehdr->e_shoff +
			(i * sizeof(Elf64_Shdr));

		if (!(shdr->sh_flags & SHF_ALLOC) ||
		    shdr->sh_addr == 0 || shdr->sh_size == 0) {
			continue;
		}

		if (strtab) {
			debug("%sing %s @ 0x%08lx (%ld bytes)\n",
			      (shdr->sh_type == SHT_NOBITS) ? "Clear" : "Load",
			       &strtab[shdr->sh_name],
			       (unsigned long)shdr->sh_addr,
			       (long)shdr->sh_size);
		}

		dst = map_sysmem(shdr->sh_addr, shdr->sh_size);
		if (shdr->sh_type == SHT_NOBITS) {
			memset(dst, 0, shdr->sh_size);
		} else {
			image = base + (ulong)shdr->sh_offset;
			memcpy(dst, (const void *)image, shdr->sh_size);
		}
		unmap_sysmem(dst);
		flush_cache(rounddown(shdr->sh_addr, ARCH_DMA_MINALIGN),
			    roundup((shdr->sh_addr + shdr->sh_size),
				     ARCH_DMA_MINALIGN) -
				rounddown(shdr->sh_addr, ARCH_DMA_MINALIGN));
	}

	entry = ehdr->e_entry;
	if (ehdr->e_machine == EM_PPC64 && (ehdr->e_flags &
					    EF_PPC64_ELFV1_ABI)) {
		/*
		 * For the 64-bit PowerPC ELF V1 ABI, e_entry is a function
		 * descriptor pointer with the first double word being the
		 * address of the entry point of the function.
		 */
		Elf64_Addr *desc = map_sysmem(entry, sizeof(*desc));

		entry = *desc;
		unmap_sysmem(desc);
	}
	unmap_sysmem(base);

	return entry;
}

/*
 * A very simple ELF loader, assumes the image is valid, returns the
 * entry point address.
 *
 * The loader firstly reads the EFI class to see if it's a 64-bit image.
 * If yes, call the ELF64 loader. Otherwise continue with the ELF32 loader.
 */
unsigned long load_elf_image_phdr(unsigned long addr)
{
	Elf32_Ehdr *ehdr; /* Elf header structure pointer */
	Elf32_Phdr *phdr; /* Program header structure pointer */
	unsigned long entry;
	void *base;
	int i;

	/* the size is not known until the header is read, so map without it */
	base = map_sysmem(addr, 0);
	ehdr = base;
	if (ehdr->e_ident[EI_CLASS] == ELFCLASS64) {
		unmap_sysmem(base);
		return load_elf64_image_phdr(addr);
	}

	phdr = base + ehdr->e_phoff;

	/* Load each program header */
	for (i = 0; i < ehdr->e_phnum; ++i, ++phdr) {
		void *dst, *src;

		/* Only load PT_LOAD program header */
		if (phdr->p_type != PT_LOAD)
			continue;

		dst = map_sysmem(phdr->p_paddr, phdr->p_memsz);
		src = base + phdr->p_offset;

		debug("Loading phdr %i to 0x%p (%i bytes)\n",
		      i, dst, phdr->p_filesz);
		if (phdr->p_filesz)
			memcpy(dst, src, phdr->p_filesz);
		if (phdr->p_filesz != phdr->p_memsz)
			memset(dst + phdr->p_filesz, 0x00,
			       phdr->p_memsz - phdr->p_filesz);
		unmap_sysmem(dst);
		flush_cache(rounddown(phdr->p_paddr, ARCH_DMA_MINALIGN),
			    roundup(phdr->p_memsz, ARCH_DMA_MINALIGN));
	}

	entry = ehdr->e_entry;
	unmap_sysmem(base);

	return entry;
}

unsigned long load_elf_image_shdr(unsigned long addr)
{
	Elf32_Ehdr *ehdr; /* Elf header structure pointer */
	Elf32_Shdr *shdr; /* Section header structure pointer */
	unsigned char *strtab = 0; /* String table pointer */
	unsigned char *image; /* Binary image pointer */
	unsigned long entry;
	void *base;
	int i; /* Loop counter */

	/* the size is not known until the header is read, so map without it */
	base = map_sysmem(addr, 0);
	ehdr = base;
	if (ehdr->e_ident[EI_CLASS] == ELFCLASS64) {
		unmap_sysmem(base);
		return load_elf64_image_shdr(addr);
	}

	/* Find the section header string table for output info */
	shdr = base + ehdr->e_shoff +
		(ehdr->e_shstrndx * sizeof(Elf32_Shdr));

	if (shdr->sh_type == SHT_STRTAB)
		strtab = base + shdr->sh_offset;

	/* Load each appropriate section */
	for (i = 0; i < ehdr->e_shnum; ++i) {
		void *dst;

		shdr = base + ehdr->e_shoff + (i * sizeof(Elf32_Shdr));

		if (!(shdr->sh_flags & SHF_ALLOC) ||
		    shdr->sh_addr == 0 || shdr->sh_size == 0) {
			continue;
		}

		if (strtab) {
			debug("%sing %s @ 0x%08lx (%ld bytes)\n",
			      (shdr->sh_type == SHT_NOBITS) ? "Clear" : "Load",
			       &strtab[shdr->sh_name],
			       (unsigned long)shdr->sh_addr,
			       (long)shdr->sh_size);
		}

		dst = map_sysmem(shdr->sh_addr, shdr->sh_size);
		if (shdr->sh_type == SHT_NOBITS) {
			memset(dst, 0, shdr->sh_size);
		} else {
			image = base + shdr->sh_offset;
			memcpy(dst, (const void *)image, shdr->sh_size);
		}
		unmap_sysmem(dst);
		flush_cache(rounddown(shdr->sh_addr, ARCH_DMA_MINALIGN),
			    roundup((shdr->sh_addr + shdr->sh_size),
				    ARCH_DMA_MINALIGN) -
			    rounddown(shdr->sh_addr, ARCH_DMA_MINALIGN));
	}

	entry = ehdr->e_entry;
	unmap_sysmem(base);

	return entry;
}

/*
 * Determine if a valid ELF image exists at the given memory location.
 * First look at the ELF header magic field, then make sure that it is
 * executable.
 */
int valid_elf_image(unsigned long addr)
{
	Elf32_Ehdr *ehdr; /* Elf header structure pointer */
	int valid = 1;

	ehdr = map_sysmem(addr, sizeof(*ehdr));

	if (!IS_ELF(*ehdr)) {
		printf("## No elf image at address 0x%08lx\n", addr);
		valid = 0;
	} else if (ehdr->e_type != ET_EXEC) {
		printf("## Not a 32-bit elf image at address 0x%08lx\n", addr);
		valid = 0;
	}
	unmap_sysmem(ehdr);

	return valid;
}
