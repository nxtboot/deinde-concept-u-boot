# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2000-2007
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

obj-$(CONFIG_BLK) += blk-uclass.o

ifndef CONFIG_BLK
obj-$(CONFIG_SPL_LEGACY_BLOCK) += blk_legacy.o
endif

ifndef CONFIG_XPL_BUILD
obj-$(CONFIG_IDE) += ide.o
obj-$(CONFIG_BLK_LUKS) += luks.o luks2.o
obj-$(CONFIG_RKMTD) += rkmtd.o
endif
obj-$(CONFIG_SANDBOX) += sandbox.o sandbox-bootdev.o host-uclass.o host_dev.o
obj-$(CONFIG_BLOCK_CACHE) += blkcache.o
obj-$(CONFIG_BLKMAP) += blkmap.o
obj-$(CONFIG_BLK_LUKS) += blkmap_crypt.o
obj-$(CONFIG_BLKMAP) += blkmap_helper.o

obj-$(CONFIG_EFI_MEDIA) += efi-media-uclass.o
obj-$(CONFIG_EFI_MEDIA_SANDBOX) += sb_efi_media.o
obj-$(CONFIG_EFI_MEDIA_BLK) += efi_blk.o

