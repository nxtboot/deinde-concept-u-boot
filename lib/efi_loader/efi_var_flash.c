// SPDX-License-Identifier: GPL-2.0+
/*
 * Memory-mapped flash store for UEFI variables
 *
 * The store is an image of the variable buffer (struct efi_var_file) at a
 * fixed address in a NOR flash which uses the Intel command set, such as
 * QEMU's pflash on x86. Reading it is a matter of reading memory. Writing it
 * means erasing the blocks the image occupies and programming them again,
 * which needs nothing but the flash itself, so it can be done at runtime as
 * well: a variable which the OS sets stays set across a cold boot.
 *
 * This is a stop-gap and knowingly duplicates what U-Boot has elsewhere:
 *
 * - The CFI programming here is a minimal copy of what drivers/mtd/cfi_flash.c
 *   does properly (bus widths, command sets, timeouts, locking). It cannot be
 *   used since it needs the driver model, which is gone at runtime.
 * - The layout of the flash (address, erase-block size) comes from Kconfig
 *   rather than from the flash itself or the devicetree, for the same reason.
 * - Only a byte-wide Intel-command-set flash is handled and every byte is
 *   programmed separately, which is slow on a large store.
 *
 * The proper solution is a runtime phase of U-Boot, with drivers built for
 * it, so that a flash driver can serve the runtime services. Until then this
 * keeps to the one thing it must do at runtime and should not grow.
 *
 * Copyright 2026 Google LLC
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi_loader.h>
#include <efi_variable.h>
#include <malloc.h>
#include <mapmem.h>
#include <asm/io.h>

/* Intel command set */
#define CFI_CMD_READ_ARRAY	0xff
#define CFI_CMD_READ_QUERY	0x98
#define CFI_CMD_CLEAR_STATUS	0x50
#define CFI_CMD_PROGRAM		0x40
#define CFI_CMD_WRITE_BUFFER	0xe8
#define CFI_CMD_ERASE		0x20
#define CFI_CMD_CONFIRM		0xd0

#define CFI_STATUS_READY	BIT(7)
/* erase error, program error, VPP low, block locked */
#define CFI_STATUS_ERROR	(BIT(5) | BIT(4) | BIT(3) | BIT(1))

/* offsets in the query data, with an 8-bit bus */
#define CFI_QUERY_OFFSET	0x10	/* "QRY" */
#define CFI_QUERY_BUF_SIZE	0x2a	/* log2 of the write-buffer size */

/* how many status reads to allow before giving up on an operation */
#define CFI_TIMEOUT		10000000

/**
 * efi_var_flash_wait() - Wait for an operation to complete
 *
 * @addr: Address the operation was started on
 * Return: 0 if OK, -ve if it failed or timed out
 */
static efi_status_t __efi_runtime efi_var_flash_wait(u8 *addr)
{
	efi_status_t ret = EFI_DEVICE_ERROR;
	u8 status;
	int i;

	for (i = 0; i < CFI_TIMEOUT; i++) {
		status = readb(addr);
		if (status & CFI_STATUS_READY) {
			if (!(status & CFI_STATUS_ERROR))
				ret = EFI_SUCCESS;
			break;
		}
	}
	if (ret != EFI_SUCCESS)
		writeb(CFI_CMD_CLEAR_STATUS, addr);
	writeb(CFI_CMD_READ_ARRAY, addr);

	return ret;
}

/**
 * efi_var_flash_erase() - Erase the block holding an address
 *
 * @addr: Address in the block
 * Return: status code
 */
static efi_status_t __efi_runtime efi_var_flash_erase(u8 *addr)
{
	writeb(CFI_CMD_ERASE, addr);
	writeb(CFI_CMD_CONFIRM, addr);

	return efi_var_flash_wait(addr);
}

/**
 * efi_var_flash_program() - Program one byte
 *
 * @addr: Address to program, which must be erased
 * @val: Value to program
 * Return: status code
 */
static efi_status_t __efi_runtime efi_var_flash_program(u8 *addr, u8 val)
{
	writeb(CFI_CMD_PROGRAM, addr);
	writeb(val, addr);

	return efi_var_flash_wait(addr);
}

/**
 * efi_var_flash_program_buf() - Program a run of bytes through the write buffer
 *
 * @addr: Address to program, which must be erased
 * @data: Bytes to program
 * @len: Number of bytes, at most the buffer size and not crossing a
 *	buffer-size boundary
 * Return: status code
 */
static efi_status_t __efi_runtime efi_var_flash_program_buf(u8 *addr,
							    const u8 *data,
							    uint len)
{
	int i;

	/* the flash says when the buffer is free */
	for (i = 0; i < CFI_TIMEOUT; i++) {
		writeb(CFI_CMD_WRITE_BUFFER, addr);
		if (readb(addr) & CFI_STATUS_READY)
			break;
	}
	if (i == CFI_TIMEOUT) {
		writeb(CFI_CMD_READ_ARRAY, addr);
		return EFI_DEVICE_ERROR;
	}
	writeb(len - 1, addr);
	for (i = 0; i < len; i++)
		writeb(data[i], addr + i);
	writeb(CFI_CMD_CONFIRM, addr);

	return efi_var_flash_wait(addr);
}

/**
 * efi_var_flash_write() - Write a variable store to the flash
 *
 * Erases the blocks the store needs and programs the store into them. Nothing
 * outside those blocks is touched, so a store which shrinks leaves stale data
 * behind it, which is harmless since the header gives its length.
 *
 * @buf: Store to write
 * @len: Length of the store in bytes
 * Return: status code
 */
static efi_status_t __efi_runtime efi_var_flash_write(const void *buf,
						      efi_uintn_t len)
{
	const struct efi_var *var = &efis->var;
	const u8 *data = buf;
	efi_status_t ret;
	efi_uintn_t i;

	if (!var->flash)
		return EFI_UNSUPPORTED;
	if (len > EFI_VAR_BUF_SIZE)
		return EFI_OUT_OF_RESOURCES;

	for (i = 0; i < len; i += CONFIG_EFI_VARIABLE_FLASH_SECTOR_SIZE) {
		ret = efi_var_flash_erase(var->flash + i);
		if (ret != EFI_SUCCESS)
			return ret;
	}

	/*
	 * Program through the write buffer where there is one, since the
	 * emulated flash of a VM may well update its backing file on every
	 * operation, and byte by byte otherwise. An erased byte reads as
	 * 0xff, so a run of those needs no programming
	 */
	for (i = 0; i < len;) {
		uint n = var->flash_bufsize ? : 1;
		uint j;

		n -= i & (n - 1);
		if (i + n > len)
			n = len - i;
		for (j = 0; j < n && data[i + j] == 0xff; j++)
			;
		if (j == n) {
			i += n;
			continue;
		}
		if (n == 1)
			ret = efi_var_flash_program(var->flash + i, data[i]);
		else
			ret = efi_var_flash_program_buf(var->flash + i,
							data + i, n);
		if (ret != EFI_SUCCESS)
			return ret;
		i += n;
	}

	for (i = 0; i < len; i++) {
		if (readb(var->flash + i) != data[i])
			return EFI_DEVICE_ERROR;
	}

	return EFI_SUCCESS;
}

/**
 * efi_var_flash_notify_virtual_address_map() - SetVirtualAddressMap callback
 *
 * @event: callback event
 * @context: callback context
 */
static void EFIAPI __efi_runtime
efi_var_flash_notify_virtual_address_map(struct efi_event *event,
					 void *context)
{
	efi_convert_pointer(0, (void **)&efis->var.flash);
}

/**
 * efi_var_flash_init() - Find the flash and make it usable at runtime
 *
 * Checks that a CFI flash answers at the configured address, adds it to the
 * memory map as runtime MMIO, so that the OS maps it and includes it in
 * SetVirtualAddressMap(), and arranges for the pointer to follow.
 *
 * Return: status code
 */
static efi_status_t efi_var_flash_init(void)
{
	struct efi_var *evar = &efis->var;
	struct efi_event *event;
	efi_status_t ret;
	bool found;
	u8 *flash;

	flash = map_sysmem(CONFIG_EFI_VARIABLE_FLASH_ADDR, EFI_VAR_BUF_SIZE);
	writeb(CFI_CMD_READ_QUERY, flash);
	found = readb(flash + CFI_QUERY_OFFSET) == 'Q' &&
		readb(flash + CFI_QUERY_OFFSET + 1) == 'R' &&
		readb(flash + CFI_QUERY_OFFSET + 2) == 'Y';
	if (found && readb(flash + CFI_QUERY_BUF_SIZE))
		evar->flash_bufsize =
			1 << readb(flash + CFI_QUERY_BUF_SIZE);
	writeb(CFI_CMD_READ_ARRAY, flash);
	if (!found) {
		log_info("No EFI variables loaded: no flash at %x\n",
			 CONFIG_EFI_VARIABLE_FLASH_ADDR);
		return EFI_NOT_FOUND;
	}

	ret = efi_add_memory_map(CONFIG_EFI_VARIABLE_FLASH_ADDR,
				 EFI_VAR_BUF_SIZE, EFI_MMAP_IO);
	if (ret != EFI_SUCCESS)
		return ret;
	ret = efi_create_event(EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE, TPL_CALLBACK,
			       efi_var_flash_notify_virtual_address_map, NULL,
			       NULL, &event);
	if (ret != EFI_SUCCESS)
		return ret;
	evar->flash = flash;

	return EFI_SUCCESS;
}

efi_status_t efi_var_to_storage(void)
{
	static bool once;
	struct efi_var_file *buf;
	efi_status_t ret;
	loff_t len;

	if (!efis->var.flash) {
		if (!once) {
			log_warning("Cannot persist EFI variables without a flash\n");
			once = true;
		}
		return EFI_UNSUPPORTED;
	}

	ret = efi_var_collect(&buf, &len, EFI_VARIABLE_NON_VOLATILE);
	if (ret != EFI_SUCCESS)
		goto error;

	ret = efi_var_flash_write(buf, len);
	free(buf);
error:
	if (ret != EFI_SUCCESS)
		log_err("Failed to persist EFI variables\n");

	return ret;
}

efi_status_t efi_var_from_storage(void)
{
	const struct efi_var_file *buf;

	if (efi_var_flash_init() != EFI_SUCCESS)
		return EFI_SUCCESS;

	buf = (const struct efi_var_file *)efis->var.flash;
	if (buf->magic != EFI_VAR_FILE_MAGIC) {
		log_info("No EFI variables loaded\n");
		return EFI_SUCCESS;
	}
	/*
	 * This checks the rest of the header and reports a bad store. Unlike a
	 * file on the EFI system partition, the flash is the firmware's own
	 * storage, as it is for OVMF, so the authenticated variables which
	 * enable secure boot (PK, KEK, db, dbx) are restored from it too.
	 */
	efi_var_restore((struct efi_var_file *)buf, true);

	return EFI_SUCCESS;
}

efi_status_t __efi_runtime efi_var_flash_sync(void)
{
	struct efi_var_file *buf = efi_var_mem_get_buf();

	return efi_var_flash_write(buf, buf->length);
}
