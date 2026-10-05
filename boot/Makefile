# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2004-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

ifndef CONFIG_XPL_BUILD

obj-$(CONFIG_BOOT_RETRY) += bootretry.o
obj-$(CONFIG_BOOT) += bootm.o bootm_os.o
obj-$(CONFIG_SUPPORT_EXTENSION_SCAN) += extension-uclass.o

obj-$(CONFIG_PXE_UTILS) += pxe_utils.o pxe_parse.o

endif

obj-y += image.o image-board.o bootm_final.o

obj-$(CONFIG_ANDROID_AB) += android_ab.o
obj-$(CONFIG_ANDROID_BOOT_IMAGE) += image-android.o image-android-dt.o

obj-$(CONFIG_BOOTCTL) += bootctl/

obj-$(CONFIG_BOOTSTD) += bootdev-uclass.o
obj-$(CONFIG_BOOTSTD) += bootflow.o
obj-$(CONFIG_BOOTSTD) += bootmeth-uclass.o
obj-$(CONFIG_BOOTSTD) += bootstd-uclass.o

obj-$(CONFIG_BOOTSTD_MENU) += bootflow_menu.o
obj-$(CONFIG_BOOTSTD_PROG) += prog_boot.o

obj-$(CONFIG_BOOTMETH_EXTLINUX) += ext_pxe_common.o bootmeth_extlinux.o
obj-$(CONFIG_BOOTMETH_EXTLINUX_PXE) += ext_pxe_common.o bootmeth_pxe.o
obj-$(CONFIG_BOOTMETH_BLS) += bls_parse.o bootmeth_bls.o
obj-$(CONFIG_BOOTMETH_EFI) += bootmeth_efi.o
obj-$(CONFIG_BOOTMETH_CROS) += bootmeth_cros.o
obj-$(CONFIG_BOOTMETH_FEL) += bootmeth_fel.o
obj-$(CONFIG_BOOTMETH_QFW) += bootmeth_qfw.o
obj-$(CONFIG_BOOTMETH_RAUC) += bootmeth_rauc.o
obj-$(CONFIG_BOOTMETH_SANDBOX) += bootmeth_sandbox.o
obj-$(CONFIG_BOOTMETH_SCRIPT) += bootmeth_script.o
obj-$(CONFIG_CEDIT) += cedit.o
obj-$(CONFIG_BOOTMETH_EFI_BOOTMGR) += bootmeth_efi_mgr.o

obj-$(CONFIG_OF_LIBFDT) += fdt_support.o
obj-$(CONFIG_FDT_SIMPLEFB) += fdt_simplefb.o

obj-$(CONFIG_UPL) += upl_common.o
obj-$(CONFIG_UPL_READ) += upl_read.o
obj-$(CONFIG_UPL_WRITE) += upl_write.o

obj-$(CONFIG_OF_LIBFDT) += image-fdt.o
obj-$(CONFIG_FIT_SIGNATURE) += fdt_region.o
obj-$(CONFIG_FIT) += image-fit.o
obj-$(CONFIG_FIT_PRINT) += fit_print.o
obj-$(CONFIG_MULTI_DTB_FIT) += boot_fit.o common_fit.o
obj-$(CONFIG_IMAGE_PRE_LOAD) += image-pre-load.o
obj-$(CONFIG_IMAGE_SIGN_INFO) += image-sig.o
obj-$(CONFIG_FIT_SIGNATURE) += image-fit-sig.o
obj-$(CONFIG_FIT_CIPHER) += image-cipher.o

obj-$(CONFIG_CMD_ADTIMG) += image-android-dt.o

obj-$(CONFIG_LOAD_FIT) += common_fit.o

obj-$(CONFIG_EXPO) += expo.o scene.o expo_build.o
obj-$(CONFIG_EXPO_DUMP) += expo_dump.o
obj-$(CONFIG_EXPO) += scene_menu.o scene_textline.o scene_textedit.o scene_txtin.o
obj-$(CONFIG_EXPO_EDITENV) += editenv.o
obj-$(CONFIG_EXPO_TEST) += expo_test.o
ifdef CONFIG_COREBOOT_SYSINFO
obj-$(CONFIG_EXPO) += expo_build_cb.o
endif

obj-$(CONFIG_BOOTMETH_VBE) += vbe.o
obj-$(CONFIG_BOOTMETH_VBE_REQUEST) += vbe_request.o
obj-$(CONFIG_BOOTMETH_VBE_SIMPLE) += vbe_simple.o vbe_common.o
obj-$(CONFIG_BOOTMETH_VBE_SIMPLE_FW) += vbe_simple_fw.o
obj-$(CONFIG_BOOTMETH_VBE_SIMPLE_OS) += vbe_simple_os.o

obj-$(CONFIG_BOOTMETH_ANDROID) += bootmeth_android.o

obj-$(CONFIG_BOOTMETH_VBE_ABREC) += vbe_abrec.o vbe_common.o
obj-$(CONFIG_BOOTMETH_VBE_ABREC_FW) += vbe_abrec_fw.o
obj-$(CONFIG_BOOTMETH_VBE_ABREC_OS) += vbe_abrec_os.o
