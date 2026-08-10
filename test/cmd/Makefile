# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (c) 2013 Google, Inc
# Copyright 2022-2023 Arm Limited and/or its affiliates <open-source-office@arm.com>

obj-$(CONFIG_$(PHASE_)CMDLINE) += command.o
ifdef CONFIG_HUSH_PARSER
obj-$(CONFIG_CONSOLE_RECORD) += test_echo.o
endif
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_HUSH_OLD_PARSER) += showvar.o
obj-$(CONFIG_ENV_IS_IN_SANDBOX) += eraseenv.o
endif
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_CMD_PAUSE) += test_pause.o
endif
obj-$(CONFIG_HUSH_PARSER) += exit.o
obj-$(CONFIG_X86) += cpuid.o msr.o
obj-$(CONFIG_CMD_ADDR_FIND) += addr_find.o
obj-$(CONFIG_CMD_ADDRMAP) += addrmap.o
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_CMD_ACPI) += acpi.o
endif
obj-$(CONFIG_CMD_BACKTRACE) += backtrace.o
obj-$(CONFIG_CMD_BDI) += bdinfo.o
obj-$(CONFIG_CMD_BOOTSTAGE) += bootstage.o
obj-$(CONFIG_CMD_CHID) += chid.o
obj-$(CONFIG_CMD_CONFIG) += config.o
obj-$(CONFIG_COREBOOT_SYSINFO) += coreboot.o
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_CMD_EDITENV) += editenv.o
endif
obj-$(CONFIG_CMD_FDT) += fdt.o
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_CMD_GREPENV) += grepenv.o
endif
obj-$(CONFIG_CMD_HASH) += hash.o
obj-$(CONFIG_CMD_HISTORY) += history.o
obj-$(CONFIG_CMD_I3C) += i3c.o
obj-$(CONFIG_CMD_LOADM) += loadm.o
obj-$(CONFIG_CMD_MALLOC) += malloc.o
ifdef CONFIG_SANDBOX
obj-$(CONFIG_CMD_MEMINFO) += meminfo.o
endif
obj-$(CONFIG_CMD_MEMORY) += mem_copy.o
obj-$(CONFIG_CMD_MEM_SEARCH) += mem_search.o
ifdef CONFIG_CONSOLE_RECORD
obj-$(CONFIG_CMD_MEMORY) += mm.o
endif
obj-$(CONFIG_CMD_PART) += part.o
obj-$(CONFIG_CMD_PART_FIND) += part_find.o
ifdef CONFIG_CMD_PCI
obj-$(CONFIG_CMD_PCI_MPS) += pci_mps.o
endif
obj-$(CONFIG_CMD_QFW) += qfw.o
obj-$(CONFIG_CMD_SEAMA) += seama.o
ifdef CONFIG_SANDBOX
obj-$(CONFIG_CONSOLE_TRUETYPE) += font.o
obj-$(CONFIG_CMD_MBR) += mbr.o
obj-$(CONFIG_CMD_PINMUX) += pinmux.o
obj-$(CONFIG_CMD_PWM) += pwm.o
obj-$(CONFIG_CMD_READ) += rw.o
obj-y += sb.o
obj-$(CONFIG_CMD_SETEXPR) += setexpr.o
obj-$(CONFIG_CMD_SMBIOS) += smbios.o
obj-$(CONFIG_CMD_TEMPERATURE) += temperature.o
obj-$(CONFIG_CMD_TKEY) += tkey.o
ifdef CONFIG_NET_LEGACY
obj-$(CONFIG_CMD_WGET) += wget.o
endif
obj-$(CONFIG_ARM_FFA_TRANSPORT) += armffa.o
endif
obj-$(CONFIG_CMD_SPAWN) += spawn.o
ifdef CONFIG_CMD_ZIP
obj-$(CONFIG_CMD_UNZIP) += unzip.o
endif
