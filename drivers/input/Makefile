# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2000-2007
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

obj-$(CONFIG_CROS_EC_KEYB) += cros_ec_keyb.o
obj-$(CONFIG_OF_CONTROL) += key_matrix.o
obj-$(CONFIG_DM_KEYBOARD) += input.o keyboard-uclass.o
obj-$(CONFIG_BUTTON_KEYBOARD) += button_kbd.o
obj-$(CONFIG_CPCAP_POWER_BUTTON) += cpcap_pwrbutton.o

obj-$(CONFIG_EFI_KEYB) += efi_keyb.o

ifndef CONFIG_XPL_BUILD

obj-$(CONFIG_APPLE_SPI_KEYB) += apple_spi_kbd.o
obj-$(CONFIG_I8042_KEYB) += i8042.o
obj-$(CONFIG_TEGRA_KEYBOARD) += input.o tegra-kbc.o
obj-$(CONFIG_TWL4030_INPUT) += twl4030.o
endif

obj-$(CONFIG_MOUSE) += mouse-uclass.o
ifdef CONFIG_MOUSE
obj-$(CONFIG_SANDBOX) += sandbox_mouse.o
endif
obj-$(CONFIG_USB_MOUSE) += usb_mouse.o
obj-$(CONFIG_EFI_MOUSE) += efi_mouse.o
