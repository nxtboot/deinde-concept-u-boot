# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2025 Simon Glass <sjg@chromium.org>

obj-$(CONFIG_VIDEO_LOGO) += deinde.o
ifdef CONFIG_MOUSE
obj-$(CONFIG_EXPO) += riscos_arrow.o
endif

ifdef CONFIG_$(PHASE_)GENERATE_ACPI_TABLE
obj-y += bgrt.o
endif

obj-$(CONFIG_BOOTCTL) += canonical.o
obj-$(CONFIG_BOOTCTL) += tick.o
obj-$(CONFIG_BOOTCTL) += multipass.o settings.o
obj-$(CONFIG_BOOTCTL) += help.o
obj-$(CONFIG_BOOTCTL) += lock.o
