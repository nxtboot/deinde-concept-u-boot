# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2000-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

ifndef CONFIG_XPL_BUILD

obj-$(CONFIG_EFI) += efi/
obj-$(CONFIG_EFI_CLIENT) += efi_client/
obj-$(CONFIG_EFI_LOADER) += efi_driver/
obj-$(CONFIG_EFI_LOADER) += efi_loader/
obj-$(CONFIG_CMD_BOOTEFI_SELFTEST) += efi_selftest/
obj-$(CONFIG_FWU_MULTI_BANK_UPDATE) += fwu_updates/
obj-$(CONFIG_LZMA) += lzma/
obj-$(CONFIG_BZIP2) += bzip2/
obj-$(CONFIG_FIT) += libfdt/
obj-$(CONFIG_OF_LIVE) += of_live.o
obj-$(CONFIG_CMD_DHRYSTONE) += dhry/
obj-$(CONFIG_ARCH_AT91) += at91/
obj-$(CONFIG_OPTEE_LIB) += optee/

obj-$(CONFIG_AES) += aes.o
obj-$(CONFIG_AES) += aes/
obj-$(CONFIG_BINMAN_FDT) += binman.o

obj-$(CONFIG_FW_LOADER) += fw_loader.o

ifndef LEGACY_API_BUILD
ifneq ($(CONFIG_CHARSET),)
obj-y += charset.o
endif
endif

obj-$(CONFIG_CHID) += chid.o

obj-$(CONFIG_CIRCBUF) += circbuf.o

obj-y += crc8.o
obj-$(CONFIG_ERRNO_STR) += errno_str.o
obj-$(CONFIG_JSON) += json.o
obj-$(CONFIG_FIT) += fdtdec_common.o
obj-$(CONFIG_TEST_FDTDEC) += fdtdec_test.o
obj-$(CONFIG_GZIP_COMPRESSED) += gzip.o

# With QEMU the SMBIOS tables come from there, not from U-Boot
ifndef CONFIG_QFW_SMBIOS
obj-$(CONFIG_GENERATE_SMBIOS_TABLE) += smbios.o
endif

obj-$(CONFIG_SMBIOS_PARSER) += smbios-parser.o
obj-$(CONFIG_IMAGE_SPARSE) += image-sparse.o
obj-y += ldiv.o
obj-$(CONFIG_XXHASH) += xxhash.o
obj-y += net_utils.o
obj-$(CONFIG_PHYSMEM) += physmem.o
obj-y += rc4.o
obj-$(CONFIG_RBTREE)	+= rbtree.o
obj-$(CONFIG_BITREVERSE) += bitrev.o
obj-$(CONFIG_SANDBOX) += find_bit.o
obj-y += list_sort.o
obj-$(CONFIG_PMBUS) += pmbus.o
endif

obj-$(CONFIG_TPM) += tpm-common.o
ifeq ($(CONFIG_TPM),y)
obj-$(CONFIG_TPM) += tpm_api.o
obj-$(CONFIG_TPM_V1) += tpm-v1.o
obj-$(CONFIG_TPM_V2) += tpm-v2.o
obj-$(CONFIG_EFI_TCG2_PROTOCOL) += tpm_tcg2.o
obj-$(CONFIG_MEASURED_BOOT) += tpm_tcg2.o
endif

obj-$(CONFIG_CRC8) += crc8.o
obj-$(CONFIG_CRC16) += crc16.o
obj-$(CONFIG_CRC16) += crc16-ccitt.o

obj-y += crypto/

obj-$(CONFIG_ACPI) += acpi/
obj-$(CONFIG_ECDSA) += ecdsa/
obj-$(CONFIG_RSA) += rsa/
obj-$(CONFIG_HASH) += hash-checksum.o

# argon2 provides its own blake2b; just build blake2s when ARGON2 is enabled
obj-$(CONFIG_BLAKE2) += blake2/blake2b.o blake2/blake2s.o
obj-$(CONFIG_ARGON2) += blake2/blake2s.o

obj-$(CONFIG_MD5_LEGACY) += md5.o
obj-$(CONFIG_SHA1_LEGACY) += sha1.o
obj-$(CONFIG_SHA256) += sha256_common.o
obj-$(CONFIG_SHA256_LEGACY) += sha256.o
obj-$(CONFIG_SHA512_LEGACY) += sha512.o
obj-$(CONFIG_SM3) += sm3.o

obj-$(CONFIG_CRYPT_PW) += crypt/
obj-$(CONFIG_ARGON2) += argon2/
obj-$(CONFIG_ASN1_DECODER_LEGACY) += asn1_decoder.o

obj-$(CONFIG_ZLIB) += zlib/
obj-$(CONFIG_ZSTD) += zstd/
obj-$(CONFIG_GZIP) += gunzip.o
obj-$(CONFIG_LZO) += lzo/
obj-$(CONFIG_LZMA) += lzma/
obj-$(CONFIG_LZ4) += lz4_wrapper.o

obj-$(CONFIG_DDR5_SPD) += ddr5_spd.o
obj-$(CONFIG_LIB_RATIONAL) += rational.o

obj-$(CONFIG_LIBAVB) += libavb/

obj-$(CONFIG_OF_LIBFDT) += libfdt/
obj-$(CONFIG_OF_REAL) += fdtdec_common.o fdtdec.o

obj-$(CONFIG_MBEDTLS_LIB) += mbedtls/

obj-$(CONFIG_NET_LWIP) += lwip/

ifdef CONFIG_XPL_BUILD
obj-$(CONFIG_SPL_YMODEM_SUPPORT) += crc16-ccitt.o
obj-$(CONFIG_HASH) += crc16-ccitt.o
obj-$(CONFIG_MMC_SPI_CRC_ON) += crc16-ccitt.o
obj-y += net_utils.o
endif
obj-$(CONFIG_ADDR_MAP) += addr_map.o
obj-y += qsort.o
obj-y += hashtable.o
obj-y += errno.o
obj-y += display_options.o
CFLAGS_display_options.o := $(if $(BUILD_TAG),-DBUILD_TAG='"$(BUILD_TAG)"')
obj-$(CONFIG_BCH) += bch.o
obj-$(CONFIG_MMC_SPI) += crc7.o
obj-$(CONFIG_CRC32) += crc32.o
obj-$(CONFIG_CRC32C) += crc32c.o
obj-y += ctype.o
obj-y += div64.o
obj-$(CONFIG_OF_LIBFDT) += fdtdec.o fdtdec_common.o fdt_print.o
obj-y += hang.o
obj-y += linux_compat.o
obj-y += linux_string.o
obj-$(CONFIG_LMB) += lmb.o
obj-y += membuf.o
obj-$(CONFIG_REGEX) += slre.o
obj-y += string.o
obj-y += tables_csum.o
obj-y += time.o
obj-y += hexdump.o
obj-$(CONFIG_GETOPT) += getopt.o
obj-$(CONFIG_TRACE) += trace.o
obj-$(CONFIG_LIB_UUID) += uuid.o
obj-$(CONFIG_LIB_RAND) += rand.o
obj-y += panic.o
obj-$(CONFIG_BACKTRACE) += backtrace.o
CFLAGS_backtrace.o += -DSRCTREE='"$(srctree)/"'

ifeq ($(CONFIG_XPL_BUILD),y)
# SPL U-Boot may use full-printf, tiny-printf or none at all
ifdef CONFIG_USE_TINY_PRINTF
obj-$(CONFIG_SPRINTF) += tiny-printf.o
else
obj-$(CONFIG_SPRINTF) += vsprintf.o
endif
obj-$(CONFIG_STRTO) += strto.o
obj-$(CONFIG_UFS_SUPPORT) += charset.o
else
# Main U-Boot always uses the full printf support
obj-y += vsprintf.o strto.o
obj-$(CONFIG_SSCANF) += sscanf.o
endif
obj-$(CONFIG_OID_REGISTRY) += oid_registry.o

obj-y += abuf.o
obj-y += alist.o
obj-y += date.o
obj-y += rtc-lib.o
obj-$(CONFIG_LIB_ELF) += elf.o

obj-$(CONFIG_SEMIHOSTING) += semihosting.o

obj-$(CONFIG_ULIB) += ulib/
obj-$(CONFIG_UTHREAD) += uthread.o

#
# Build a fast OID lookup registry from include/linux/oid_registry.h
#
$(obj)/oid_registry.o: $(obj)/oid_registry_data.c

$(obj)/oid_registry_data.c: $(srctree)/include/linux/oid_registry.h \
			    $(srctree)/scripts/build_OID_registry
	$(call cmd,build_OID_registry)

quiet_cmd_build_OID_registry = GEN     $@
      cmd_build_OID_registry = perl $(srctree)/scripts/build_OID_registry $< $@

clean-files     += oid_registry_data.c

subdir-ccflags-$(CONFIG_CC_OPTIMIZE_LIBS_FOR_SPEED) += -O2
