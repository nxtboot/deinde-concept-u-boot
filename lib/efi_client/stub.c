// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 *
 * EFI information obtained here:
 * http://wiki.phoenix.com/wiki/index.php/EFI_BOOT_SERVICES
 *
 * Provides helper functions for use with the stub
 */

#define LOG_CATEGORY	LOGC_EFI

#include <debug_uart.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_stub.h>
#include <linux/errno.h>

/*
 * true if we must use the hardware UART directory (EFI not available). This
 * is normally false, meaning that character output is sent to the efi_putc()
 * routine. Once exit-boot-services is called, we must either not use character
 * output at all, or use a hardware UART directly, if there is a driver
 * available.
 */
bool use_hw_uart;

int efi_stub_exit_boot_services(void)
{
	struct efi_priv *priv = efi_get_priv();
	const struct efi_boot_services *boot = priv->boot;
	efi_uintn_t size;
	u32 version;
	efi_status_t ret;

	size = priv->memmap_alloc;
	ret = boot->get_memory_map(&size, priv->memmap_desc,
				   &priv->memmap_key,
				   &priv->memmap_desc_size, &version);
	if (ret) {
		printhex2(ret);
		puts(" Can't get memory map\n");
		return ret;
	}
	ret = boot->exit_boot_services(priv->parent_image, priv->memmap_key);
	if (ret)
		return ret;

	return 0;
}

void *memcpy(void *dest, const void *src, size_t size)
{
	unsigned char *dptr = dest;
	const unsigned char *ptr = src;
	const unsigned char *end = src + size;

	while (ptr < end)
		*dptr++ = *ptr++;

	return dest;
}

void *memset(void *inptr, int ch, size_t size)
{
	char *ptr = inptr;
	char *end = ptr + size;

	while (ptr < end)
		*ptr++ = ch;

	return ptr;
}

int setup_info_table(struct efi_priv *priv, int size)
{
	struct efi_info_hdr *info;
	efi_status_t ret;

	/* Get some memory for our info table */
	priv->info_size = size;
	info = efi_malloc(priv, priv->info_size, &ret);
	if (ret) {
		printhex2(ret);
		puts(" No memory for info table: ");
		return ret;
	}

	memset(info, '\0', sizeof(*info));
	info->version = EFI_TABLE_VERSION;
	info->hdr_size = sizeof(*info);
	priv->info = info;
	priv->next_hdr = (char *)info + info->hdr_size;

	return 0;
}

/**
 * add_entry_addr() - Add a new entry to the efi_info list
 *
 * This adds an entry, consisting of a tag and two lots of data. This avoids the
 * caller having to coalesce the data first
 *
 * @priv: Pointer to our private information which contains the list
 * @type: Type of the entry to add
 * @ptr1: Pointer to first data block to add
 * @size1: Size of first data block in bytes (can be 0)
 * @ptr2: Pointer to second data block to add
 * @size2: Size of second data block in bytes (can be 0)
 */
void add_entry_addr(struct efi_priv *priv, enum efi_entry_t type, void *ptr1,
		    int size1, void *ptr2, int size2)
{
	struct efi_entry_hdr *hdr = priv->next_hdr;

	hdr->type = type;
	hdr->size = size1 + size2;
	hdr->addr = 0;
	hdr->link = ALIGN(sizeof(*hdr) + hdr->size, 16);
	priv->next_hdr += hdr->link;
	memcpy(hdr + 1, ptr1, size1);
	memcpy((void *)(hdr + 1) + size1, ptr2, size2);
	priv->info->total_size = (ulong)priv->next_hdr - (ulong)priv->info;
}

static void efi_copy_code(struct efi_priv *priv)
{
	memcpy((void *)priv->jump_addr, _binary_u_boot_bin_start,
	       (ulong)_binary_u_boot_bin_end -
	       (ulong)_binary_u_boot_bin_start);
}

/**
 * efi_store_memory_map() - Collect the memory-map info from EFI
 *
 * Collect the memory info and store it for later use, e.g. in calling
 * exit_boot_services()
 *
 * @priv:	Pointer to private EFI structure
 * Returns: 0 if OK, non-zero on error
 */
static int efi_store_memory_map(struct efi_priv *priv)
{
	struct efi_boot_services *boot = priv->sys_table->boottime;
	efi_uintn_t size, desc_size;
	efi_status_t ret;

	/* Get the memory map so we can switch off EFI */
	size = 0;
	ret = boot->get_memory_map(&size, NULL, &priv->memmap_key,
				   &priv->memmap_desc_size,
				   &priv->memmap_version);
	if (ret != EFI_BUFFER_TOO_SMALL) {
		/*
		 * Note this function avoids using printf() since it is not
		 * available in the stub
		 */
		printhex2(EFI_BITS_PER_LONG);
		putc(' ');
		printhex2(ret);
		puts(" No memory map\n");
		return -EFAULT;
	}
	/*
	 * Since doing a malloc() may change the memory map and also we want to
	 * be able to read the memory map in efi_call_exit_boot_services()
	 * below, after more changes have happened
	 */
	priv->memmap_alloc = size + 1024;
	priv->memmap_size = priv->memmap_alloc;
	priv->memmap_desc = efi_malloc(priv, size, &ret);
	if (!priv->memmap_desc) {
		printhex2(ret);
		puts(" No memory for memory descriptor\n");
		return -EFAULT;
	}

	ret = boot->get_memory_map(&priv->memmap_size, priv->memmap_desc,
				   &priv->memmap_key, &desc_size,
				   &priv->memmap_version);
	if (ret) {
		printhex2(ret);
		puts(" Can't get memory map\n");
		return -EFAULT;
	}

	return 0;
}

/**
 * efi_main() - Start an EFI image
 *
 * This function is called by our EFI start-up code. It handles running
 * U-Boot. If it returns, EFI will continue.
 */
efi_status_t EFIAPI efi_main(efi_handle_t image,
			     struct efi_system_table *sys_table)
{
	struct efi_priv local_priv, *priv = &local_priv;
	struct efi_boot_services *boot = sys_table->boottime;
	struct efi_entry_memmap map;
	struct efi_gop *gop;
	struct efi_entry_gopmode mode;
	struct efi_entry_systable table;
	efi_guid_t efi_gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	efi_status_t ret;

	/* initially we can use the EFI UART */
	use_hw_uart = false;

	ret = efi_init(priv, "Payload", image, sys_table, true);
	if (ret) {
		printhex2(ret);
		puts(" efi_init() failed\n");
		return ret;
	}
	efi_set_priv(priv);

	ret = arch_efi_main_init(priv, boot);
	if (ret)
		return ret;

	ret = efi_store_memory_map(priv);
	if (ret)
		return ret;

	ret = setup_info_table(priv, priv->memmap_size + 128);
	if (ret)
		return ret;

	ret = boot->locate_protocol(&efi_gop_guid, NULL, (void **)&gop);
	if (ret) {
		puts(" GOP unavailable\n");
	} else {
		mode.fb_base = gop->mode->fb_base;
		mode.fb_size = gop->mode->fb_size;
		mode.info_size = gop->mode->info_size;
		add_entry_addr(priv, EFIET_GOP_MODE, &mode, sizeof(mode),
			       gop->mode->info,
			       sizeof(struct efi_gop_mode_info));
	}

	table.sys_table = (ulong)sys_table;
	add_entry_addr(priv, EFIET_SYS_TABLE, &table, sizeof(table), NULL, 0);

	ret = efi_stub_exit_boot_services();
	if (ret)
		return ret;

	/* The EFI UART won't work now, switch to a debug one */
	use_hw_uart = true;

	map.version = priv->memmap_version;
	map.desc_size = priv->memmap_desc_size;
	add_entry_addr(priv, EFIET_MEMORY_MAP, &map, sizeof(map),
		       priv->memmap_desc, priv->memmap_size);
	add_entry_addr(priv, EFIET_END, NULL, 0, 0, 0);

	efi_copy_code(priv);

/* This will only work if you patched your own debug uart into this file. */
#ifdef DEBUG
	puts("EFI table at ");
	printhex8((ulong)priv->info);
	puts(" size ");
	printhex8(priv->info->total_size);
	putc('\n');
#endif
	arch_efi_jump_to_payload(priv);

	return EFI_LOAD_ERROR;
}
