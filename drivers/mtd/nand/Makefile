# SPDX-License-Identifier: GPL-2.0+

nandcore-objs := core.o bbt.o

ifeq ($(CONFIG_XPL_BUILD)$(CONFIG_TPL_BUILD),)

# U-Boot proper
obj-$(CONFIG_MTD_NAND_CORE) += nandcore.o
obj-$(CONFIG_MTD_RAW_NAND) += raw/
obj-$(CONFIG_MTD_SPI_NAND) += spi/

else

# SPL / XPL / TPL
# SPL has no MTD_NAND_CORE symbol, so we must key off SPI NAND usage
obj-$(CONFIG_SPL_SPI_NAND_LOAD) += nandcore.o
obj-$(CONFIG_SPL_SPI_NAND_LOAD) += spi/

# raw NAND still follows the normal SPL rule
obj-$(CONFIG_$(PHASE_)NAND_SUPPORT) += raw/
endif
