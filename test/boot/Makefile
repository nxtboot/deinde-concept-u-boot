# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2021 Google LLC

ifdef CONFIG_UT_BOOTSTD
obj-$(CONFIG_BOOTSTD) += bootdev.o bootstd_common.o bootflow.o bootmeth.o
obj-$(CONFIG_BOOTMETH_BLS) += bls_parse.o
obj-$(CONFIG_CMDLINE) += pxe.o
obj-$(CONFIG_FIT) += image.o
obj-$(CONFIG_$(PHASE_)FIT_PRINT) += fit_print.o
obj-$(CONFIG_BLK_LUKS) += luks.o

ifdef CONFIG_VIDEO_SANDBOX_SDL
obj-$(CONFIG_EXPO) += expo.o expo_common.o
obj-$(CONFIG_EXPO_EDITENV) += editenv.o
obj-$(CONFIG_CEDIT) += cedit.o expo_common.o
endif
obj-$(CONFIG_UT_BOOTCTL) += bootctl/
endif

ifdef CONFIG_SANDBOX
obj-$(CONFIG_$(PHASE_)CMDLINE) += bootm.o
ifdef CONFIG_UT_DM
obj-$(CONFIG_$(PHASE_)OF_LIBFDT) += image_fdt.o
endif
endif
obj-$(CONFIG_$(PHASE_)FIT_VERITY) += fit_verity.o
obj-$(CONFIG_MEASURED_BOOT) += measurement.o

ifdef CONFIG_OF_LIVE
ifndef CONFIG_SPL_BUILD
obj-$(CONFIG_BOOTMETH_VBE_SIMPLE) += vbe_simple.o vbe_read_fit.o
endif
endif
obj-$(CONFIG_BOOTMETH_VBE) += vbe_fixup.o
obj-$(CONFIG_BOOTMETH_VBE_ABREC_OS) += vbe_abrec_os.o

obj-$(CONFIG_UPL) += upl.o
