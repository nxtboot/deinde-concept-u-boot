# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

obj-$(CONFIG_TPL_BUILD) += car.o
obj-$(CONFIG_XPL_BUILD) += cpu_spl.o
obj-y += early_uart.o
obj-y += dram.o

# The LPSS UART is the same as Apollo Lake, so use its of-platdata driver
obj-y += ../apollolake/uart.o
