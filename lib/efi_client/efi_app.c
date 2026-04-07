// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2015 Google, Inc
 *
 * EFI information obtained here:
 * http://wiki.phoenix.com/wiki/index.php/EFI_BOOT_SERVICES
 *
 * This file implements U-Boot running as an EFI application.
 */

#define LOG_CATEGORY	LOGC_EFI

#include <cpu_func.h>
#include <debug_uart.h>
#include <dm.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_stub.h>
#include <efi_variable.h>
#include <errno.h>
#include <fdt_simplefb.h>
#include <image.h>
#include <init.h>
#include <malloc.h>
#include <smbios.h>
#include <sysreset.h>
#include <u-boot/uuid.h>
#include <asm/global_data.h>
#include <linux/err.h>
#include <linux/types.h>
#include <dm/device-internal.h>
#include <dm/lists.h>
#include <dm/root.h>
#include <mapmem.h>
#include <event.h>

DECLARE_GLOBAL_DATA_PTR;

int copy_uboot_to_ram(void)
{
	return 0;
}

int do_elf_reloc_fixups(void)
{
	return 0;
}

int efi_init_obj_list(void)
{
	return EFI_SUCCESS;
}

int efi_info_get(enum efi_entry_t type, void **datap, int *sizep)
{
	return -ENOSYS;
}

int efi_get_mmap(struct efi_mem_desc **descp, int *sizep, uint *keyp,
		 int *desc_sizep, uint *versionp)
{
	struct efi_priv *priv = efi_get_priv();
	struct efi_boot_services *boot = priv->sys_table->boottime;
	efi_uintn_t size, desc_size, key;
	struct efi_mem_desc *desc;
	efi_status_t ret;
	u32 version;

	/* Get the memory map so we can switch off EFI */
	size = 0;
	ret = boot->get_memory_map(&size, NULL, &key, &desc_size, &version);
	if (ret != EFI_BUFFER_TOO_SMALL)
		return log_msg_ret("get", -ENOMEM);

	desc = malloc(size);
	if (!desc)
		return log_msg_ret("mem", -ENOMEM);

	ret = boot->get_memory_map(&size, desc, &key, &desc_size, &version);
	if (ret)
		return log_msg_ret("get", -EINVAL);

	*descp = desc;
	*sizep = size;
	*desc_sizep = desc_size;
	*versionp = version;
	*keyp = key;

	return 0;
}

static efi_status_t setup_memory(struct efi_priv *priv, bool verbose)
{
	struct efi_boot_services *boot = priv->boot;
	struct global_data *ptr;
	efi_physical_addr_t addr;
	efi_status_t ret;
	int pages;

	ptr = efi_malloc(priv, sizeof(*ptr), &ret);
	if (!ptr)
		return ret;
	memset(ptr, '\0', sizeof(*ptr));

	set_gd(ptr);

	gd->malloc_base = (ulong)efi_malloc(priv, CONFIG_VAL(SYS_MALLOC_F_LEN),
					    &ret);
	if (!gd->malloc_base)
		return ret;
	pages = CONFIG_EFI_RAM_SIZE >> 12;

	/*
	 * Try not to allocate any memory above 4GB, just for ease of looking at
	 * addresses.
	 */
	addr = 1ULL << 32;
	ret = boot->allocate_pages(EFI_ALLOCATE_MAX_ADDRESS,
				   priv->image_data_type, pages, &addr);
	if (ret) {
		if (verbose)
			log_info("(any address) ");
		ret = boot->allocate_pages(EFI_ALLOCATE_ANY_PAGES,
					   priv->image_data_type, pages, &addr);
	}
	if (ret) {
		if (verbose)
			log_info("(using pool %lx) ", ret);
		priv->ram_base = (ulong)efi_malloc(priv, CONFIG_EFI_RAM_SIZE,
						   &ret);
		if (!priv->ram_base)
			return ret;
		priv->use_pool_for_malloc = true;
	} else {
		if (verbose)
			log_info("(using allocated RAM address %lx) ",
				 (ulong)addr);
		priv->ram_base = addr;
	}
	gd->ram_base = addr;
	gd->ram_size = pages << 12;

	return 0;
}

/**
 * free_memory() - Free memory used by the U-Boot app
 *
 * This frees memory allocated in setup_memory(), in preparation for returning
 * to UEFI. It also zeroes the global_data pointer.
 *
 * @priv: Private EFI data
 */
static void free_memory(struct efi_priv *priv)
{
	struct efi_boot_services *boot = priv->boot;

	if (priv->use_pool_for_malloc)
		efi_free(priv, (void *)priv->ram_base);
	else
		boot->free_pages(priv->ram_base,
				 gd->ram_size >> EFI_PAGE_SHIFT);

	efi_free(priv, (void *)gd->malloc_base);
	efi_free(priv, (void *)gd);
	set_gd((void *)NULL);
}

static void scan_tables(struct efi_system_table *sys_table)
{
	efi_guid_t acpi = EFI_ACPI_TABLE_GUID;
	efi_guid_t smbios = SMBIOS_TABLE_GUID;
	efi_guid_t smbios3 = SMBIOS3_TABLE_GUID;
	efi_guid_t fdt = EFI_FDT_GUID;
	uint i;

	for (i = 0; i < sys_table->nr_tables; i++) {
		struct efi_configuration_table *tab = &sys_table->tables[i];

		if (!memcmp(&tab->guid, &acpi, sizeof(efi_guid_t)))
			gd_set_acpi_start(map_to_sysmem(tab->table));
		else if (!memcmp(&tab->guid, &smbios, sizeof(efi_guid_t)) ||
			 !memcmp(&tab->guid, &smbios3, sizeof(efi_guid_t)))
			gd_set_smbios_start(map_to_sysmem(tab->table));
		else if (!memcmp(&tab->guid, &fdt, sizeof(efi_guid_t)))
			gd_set_firmware_fdt_addr((ulong)tab->table);
	}
}

static bool detect_emulator(void)
{
	struct smbios_info info;
	struct smbios_type1 *t1;
	const char *manufacturer;

	/* Check if running in QEMU by looking at SMBIOS manufacturer */
	if (!smbios_locate(gd_smbios_start(), &info)) {
		t1 = (void *)smbios_get_header(&info,
					SMBIOS_SYSTEM_INFORMATION);
		if (t1) {
			manufacturer = smbios_get_string(&t1->hdr,
						t1->manufacturer);
			if (manufacturer && !strcmp(manufacturer, "QEMU"))
				return true;
		}
	}

	return false;
}

static void find_protocols(struct efi_priv *priv)
{
	efi_guid_t guid = EFI_DEVICE_PATH_TO_TEXT_PROTOCOL_GUID;
	struct efi_boot_services *boot = priv->boot;

	boot->locate_protocol(&guid, NULL, (void **)&priv->efi_dp_to_text);
}

static void efi_exit(void)
{
	struct efi_priv *priv = efi_get_priv();

	printf("U-Boot EFI exiting\n");
	priv->boot->exit(priv->parent_image, EFI_SUCCESS, 0, NULL);
}

static int efi_sysreset_request(struct udevice *dev, enum sysreset_t type)
{
	struct efi_priv *priv = efi_get_priv();

	switch (type) {
	case SYSRESET_COLD:
		/* Perform a cold reset */
		priv->run->reset_system(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL);
		break;
	case SYSRESET_TO_FIRMWARE_UI: {
		efi_status_t ret;
		u64 osind;

		/* Read current OsIndications value */
		osind = 0;
		ret = efi_get_variable_int(u"OsIndications",
					   &efi_global_variable_guid,
					   NULL, NULL, &osind, NULL);
		if (ret && ret != EFI_NOT_FOUND)
			log_warning("Failed to read OsIndications: %lx\n", ret);

		/* Set the boot-to-firmware-UI bit */
		osind |= EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
		ret = efi_set_variable_int(u"OsIndications",
					   &efi_global_variable_guid,
					   EFI_VARIABLE_NON_VOLATILE |
					   EFI_VARIABLE_BOOTSERVICE_ACCESS |
					   EFI_VARIABLE_RUNTIME_ACCESS,
					   sizeof(osind), &osind, false);
		if (ret) {
			log_err("Failed to set OsIndications: %lx\n", ret);
			return -EIO;
		}
		fallthrough;
	}
	case SYSRESET_WARM:
		priv->run->reset_system(EFI_RESET_WARM, EFI_SUCCESS, 0, NULL);
		break;
	case SYSRESET_HOT:
	default:
		efi_exit();
		break;
	}

	return -EINPROGRESS;
}

/*
 * Attempt to relocate the kernel to somewhere the firmware isn't using
 */
int board_fixup_os(void *ctx, struct event *evt)
{
	int pages;
	u64 addr;
	efi_status_t status;
	struct efi_priv *priv = efi_get_priv();
	struct efi_boot_services *boot = priv->boot;
	struct event_os_load *os_load = &evt->data.os_load;

	pages = DIV_ROUND_UP(os_load->size, EFI_PAGE_SIZE);

	addr = os_load->addr;

	/* Try to allocate at the preferred address */
	status = boot->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
				      pages, &addr);
	if (!status)
		return 0;

	/* That failed, so try allocating anywhere there's enough room */
	status = boot->allocate_pages(EFI_ALLOCATE_ANY_PAGES, EFI_LOADER_DATA, pages, &addr);
	if (status) {
		printf("Failed to alloc %lx bytes: %lx\n", os_load->size,
		       status);
		return -EFAULT;
	}

	/* Make sure bootm knows where we loaded the image */
	os_load->addr = addr;

	return 0;
}
EVENT_SPY_FULL(EVT_BOOT_OS_ADDR, board_fixup_os);

int efi_app_exit_boot_services(struct efi_priv *priv, uint key)
{
	const struct efi_boot_services *boot = priv->boot;
	int ret;

	ret = boot->exit_boot_services(priv->parent_image, key);
	if (ret)
		return ret;

	return 0;
}

int ft_system_setup(void *fdt, struct bd_info *bd)
{
	struct efi_mem_desc *map, *desc, *end;
	u64 ram_start, ram_end;
	int desc_size;
	int ret, upto;
	uint version;
	int size;
	uint key;

	ret = efi_get_mmap(&map, &size, &key, &desc_size, &version);
	if (ret)
		return log_msg_ret("erm", ret);

	if (_DEBUG)
		efi_dump_mem_table(map, size, desc_size, false);

	ram_start = -1ULL;
	ram_end = -1ULL;
	end = (void *)map + size;
	for (upto = 0, desc = map; desc < end;
	     desc = efi_get_next_mem_desc(desc, desc_size), upto++) {
		u64 base = desc->physical_start, limit;

		if (!efi_mem_is_boot_services(desc->type) &&
		    desc->type != EFI_CONVENTIONAL_MEMORY)
			continue;

		if (ram_start == -1ULL)
			ram_start = base;
		limit = base + (desc->num_pages << EFI_PAGE_SHIFT);
		log_debug("%d: %s: %llx limit %llx\n", upto,
			  efi_mem_type_name(desc->type), base, limit);
		if (ram_end == -1ULL || limit > ram_end)
			ram_end = limit;
	}

	log_info("RAM extends from %llx to %llx\n", ram_start, ram_end);
	ret = fdt_fixup_memory(fdt, ram_start, ram_end - ram_start);
	if (ret) {
		printf("failed fixup memory\n");
		return ret;
	}

	if (IS_ENABLED(CONFIG_FDT_SIMPLEFB)) {
		ret = fdt_simplefb_add_node(fdt);
		if (ret)
			log_warning("failed to set up simplefb\n");
	}

	/* Compare EFI memory map with device tree reserved regions */
	ret = efi_mem_reserved_sync(fdt, true);
	if (ret)
		log_warning("failed to set up reserved memory\n");

	free(map);

	return 0;
}

static const struct udevice_id efi_sysreset_ids[] = {
	{ .compatible = "efi,reset" },
	{ }
};

static struct sysreset_ops efi_sysreset_ops = {
	.request = efi_sysreset_request,
};

U_BOOT_DRIVER(efi_sysreset) = {
	.name = "efi-sysreset",
	.id = UCLASS_SYSRESET,
	.of_match = efi_sysreset_ids,
	.ops = &efi_sysreset_ops,
};

/**
 * efi_reserve_priv() - Event handler to reserve memory for struct efi_priv
 *
 * This handler is called during EVT_RESERVE_BOARD event to reserve memory
 * for the efi_priv structure before other reservations.
 *
 * @ctx: Event context (unused)
 * @event: Event information containing reserve_board data
 * Return: 0 on success
 */
static int efi_reserve_priv(void *ctx, struct event *event)
{
	struct event_reserve_board *reserve = &event->data.reserve_board;
	struct efi_priv *priv;
	ulong addr;

	/* Reserve memory for efi_priv using the provided stack pointer */
	addr = ALIGN_DOWN(reserve->start_addr_sp - sizeof(struct efi_priv), 16);
	priv = map_sysmem(addr, sizeof(struct efi_priv));

	/* Update stack pointer for next reservation */
	reserve->start_addr_sp = addr;

	/* Move existing priv data (from stack) to reserved location */
	efi_move_priv(priv);

	log_debug("Reserving %zu bytes for EFI priv at: %08lx\n",
		  sizeof(struct efi_priv), addr);

	return 0;
}
EVENT_SPY_FULL(EVT_RESERVE_BOARD, efi_reserve_priv);

efi_status_t EFIAPI efi_startup(efi_handle_t image,
				struct efi_system_table *systab, bool is_ulib)
{
	struct efi_priv local_priv, *priv = &local_priv;
	efi_status_t ret;
	bool verbose = !is_ulib;

	/* Set up access to EFI data structures */
	ret = efi_init(priv, "App", image, systab, verbose);
	if (ret) {
		printf("Failed to set up ulib: err=%lx\n", ret);
		return ret;
	}
	efi_set_priv(priv);

	/*
	 * Set up the EFI debug UART so that printf() works. This is
	 * implemented in the EFI serial driver, serial_efi.c. The application
	 * can use printf() freely.
	 */
	debug_uart_init();

	/* allocate some memory and set gd */
	ret = setup_memory(priv, verbose);
	if (ret) {
		printf("Failed to set up memory: ret=%lx\n", ret);
		return ret;
	}

	scan_tables(priv->sys_table);
	find_protocols(priv);

	/*
	 * We could store the EFI memory map here, but it changes all the time,
	 * so this is only useful for debugging.
	 *
	 * ret = efi_store_memory_map(priv);
	 * if (ret)
	 *	return ret;
	 */

	if (!is_ulib)
		printf("starting\n");

	board_init_f(GD_FLG_SKIP_RELOC |
		     (is_ulib ? GD_FLG_ULIB : 0) |
		     (detect_emulator() ? GD_FLG_EMUL : 0));
	gd = gd->new_gd;
	board_init_r(NULL, 0);

	return 0;
}

void efi_shutdown(void)
{
	struct efi_priv *priv;

	/* TODO: this causes a crash as efi_exit() makes use of priv */
	priv = efi_get_priv();
	if (0)
		free_memory(priv);
	efi_exit();
}
