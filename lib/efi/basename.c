// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022, Heinrich Schuchardt <xypron.glpk@gmx.de>
 */

#define LOG_CATEGORY LOGC_EFI

#include <efi.h>
#include <efi_load_initrd.h>
#include <env.h>
#include <errno.h>
#include <host_arch.h>

#undef BOOTEFI_NAME

/*
 * The constants below come from:
 *
 * https://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xhtml#processor-architecture
 */

#if HOST_ARCH == HOST_ARCH_X86_64
#define HOST_BOOTEFI_NAME "BOOTX64.EFI"
#define HOST_PXE_ARCH 0x6
#elif HOST_ARCH == HOST_ARCH_X86
#define HOST_BOOTEFI_NAME "BOOTIA32.EFI"
#define HOST_PXE_ARCH 0x7
#elif HOST_ARCH == HOST_ARCH_AARCH64
#define HOST_BOOTEFI_NAME "BOOTAA64.EFI"
#define HOST_PXE_ARCH 0xb
#elif HOST_ARCH == HOST_ARCH_ARM
#define HOST_BOOTEFI_NAME "BOOTARM.EFI"
#define HOST_PXE_ARCH 0xa
#elif HOST_ARCH == HOST_ARCH_RISCV32
#define HOST_BOOTEFI_NAME "BOOTRISCV32.EFI"
#define HOST_PXE_ARCH 0x19
#elif HOST_ARCH == HOST_ARCH_RISCV64
#define HOST_BOOTEFI_NAME "BOOTRISCV64.EFI"
#define HOST_PXE_ARCH 0x1b
#else
#error Unsupported Host architecture
#endif

#if defined(CONFIG_SANDBOX)
#define BOOTEFI_NAME "BOOTSBOX.EFI"
#elif defined(CONFIG_ARM64)
#define BOOTEFI_NAME "BOOTAA64.EFI"
#elif defined(CONFIG_ARM)
#define BOOTEFI_NAME "BOOTARM.EFI"
#elif defined(CONFIG_X86_64)
#define BOOTEFI_NAME "BOOTX64.EFI"
#elif defined(CONFIG_X86)
#define BOOTEFI_NAME "BOOTIA32.EFI"
#elif defined(CONFIG_ARCH_RV32I)
#define BOOTEFI_NAME "BOOTRISCV32.EFI"
#elif defined(CONFIG_ARCH_RV64I)
#define BOOTEFI_NAME "BOOTRISCV64.EFI"
#else
#error Unsupported UEFI architecture
#endif

#if defined(CONFIG_CMD_EFIDEBUG) || defined(CONFIG_EFI_LOAD_FILE2_INITRD)
/* GUID used by Linux to identify the LoadFile2 protocol with the initrd */
const efi_guid_t efi_lf2_initrd_guid = EFI_INITRD_MEDIA_GUID;
#endif

const char *efi_get_basename(void)
{
	return efi_use_host_arch() ? HOST_BOOTEFI_NAME : BOOTEFI_NAME;
}

int efi_get_pxe_arch(void)
{
	if (efi_use_host_arch())
		return HOST_PXE_ARCH;

	/* http://www.iana.org/assignments/dhcpv6-parameters/dhcpv6-parameters.xml */
	if (IS_ENABLED(CONFIG_ARM64))
		return 0xb;
	else if (IS_ENABLED(CONFIG_ARM))
		return 0xa;
	else if (IS_ENABLED(CONFIG_X86_64))
		return 0x6;
	else if (IS_ENABLED(CONFIG_X86))
		return 0x7;
	else if (IS_ENABLED(CONFIG_ARCH_RV32I))
		return 0x19;
	else if (IS_ENABLED(CONFIG_ARCH_RV64I))
		return 0x1b;

	return -EINVAL;
}

/**
 * efi_get_distro_fdt_name() - get the filename for reading the .dtb file
 *
 * @fname:	buffer for filename
 * @size:	buffer size
 * @seq:	sequence number, to cycle through options (0=first)
 *
 * Returns:
 * 0 on success,
 * -ENOENT if the "fdtfile" env var does not exist,
 * -EINVAL if there are no more options,
 * -EALREADY if the control FDT should be used
 */
int efi_get_distro_fdt_name(char *fname, int size, int seq)
{
	const char *fdt_fname;
	const char *prefix;

	/* select the prefix */
	switch (seq) {
	case 0:
		/* this is the default */
		prefix = "/dtb";
		break;
	case 1:
		prefix = "";
		break;
	case 2:
		prefix = "/dtb/current";
		break;
	case 3:
		prefix = "/dtbs";
		break;
	default:
		return log_msg_ret("pref", -EINVAL);
	}

	fdt_fname = env_get("fdtfile");
	if (fdt_fname) {
		snprintf(fname, size, "%s/%s", prefix, fdt_fname);
		log_debug("Using device tree: %s\n", fname);
	} else if (IS_ENABLED(CONFIG_OF_HAS_PRIOR_STAGE)) {
		strcpy(fname, "<prior>");
		return log_msg_ret("pref", -EALREADY);
	/* Use this fallback only for 32-bit ARM */
	} else if (IS_ENABLED(CONFIG_ARM) && !IS_ENABLED(CONFIG_ARM64)) {
		const char *soc = env_get("soc");
		const char *board = env_get("board");
		const char *boardver = env_get("boardver");

		/* cf the code in label_boot() which seems very complex */
		snprintf(fname, size, "%s/%s%s%s%s.dtb", prefix,
			 soc ? soc : "", soc ? "-" : "", board ? board : "",
			 boardver ? boardver : "");
		log_debug("Using default device tree: %s\n", fname);
	} else {
		return log_msg_ret("env", -ENOENT);
	}

	return 0;
}
