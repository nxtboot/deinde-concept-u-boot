# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

obj-$(CONFIG_TPL_BUILD) += car.o
obj-$(CONFIG_TPL_BUILD) += fsp_t.o
obj-$(CONFIG_TPL_BUILD) += spl.o
obj-$(CONFIG_XPL_BUILD) += cpu_spl.o
obj-$(CONFIG_SPL_BUILD) += fsp_m.o
ifndef CONFIG_XPL_BUILD
obj-y += fsp_s.o
obj-$(CONFIG_GENERATE_ACPI_TABLE) += acpi.o
endif
obj-y += early_uart.o
obj-y += hostbridge.o

# The LPSS UART is the same as Apollo Lake, so use its of-platdata driver
obj-y += ../apollolake/uart.o
