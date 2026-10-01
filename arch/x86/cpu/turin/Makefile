# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

ifdef CONFIG_XPL_BUILD
obj-$(CONFIG_$(PHASE_)X86_16BIT_INIT) += car.o
obj-y += cpu.o
else
obj-$(CONFIG_GENERATE_ACPI_TABLE) += acpi.o
obj-$(CONFIG_GENERATE_ACPI_TABLE) += ivrs.o
obj-y += ap_start.o
obj-y += apob.o
obj-y += cpu.o
obj-y += cpu_dev.o
obj-y += e820.o
obj-y += mp.o
obj-y += mpio.o
obj-y += pci.o
obj-$(CONFIG_SYSINFO_SMBIOS) += smbios.o
obj-$(CONFIG_TURIN_REG_TRACE) += trace.o
obj-$(CONFIG_CMDLINE) += fch.o
obj-$(CONFIG_CMDLINE) += smn.o
obj-$(CONFIG_CMDLINE) += mca.o

# openSIL is built by its own build system, from outside U-Boot's tree; the
# glue depends on the library so that it is built before U-Boot is linked
ifdef CONFIG_TURIN_OPENSIL
obj-y += opensil.o

opensil_src := $(abspath $(CONFIG_TURIN_OPENSIL_PATH:"%"=%))
ifeq ($(wildcard $(opensil_src)/meson.build),)
# Without the tree, fail unless OPENSIL_ALLOW_MISSING is set, as for CI's
# world builds, and then build the glue alone
ifeq ($(OPENSIL_ALLOW_MISSING),)
$(error openSIL tree not found at '$(opensil_src)' \
	(CONFIG_TURIN_OPENSIL_PATH); set OPENSIL_ALLOW_MISSING=1 to build \
	without it)
endif
$(warning openSIL tree not found at '$(opensil_src)' \
	(CONFIG_TURIN_OPENSIL_PATH): building without openSIL)
CFLAGS_opensil.o += -DOPENSIL_MISSING
else
CFLAGS_opensil.o += -I$(opensil_src)/Include

$(obj)/opensil.o: $(obj)/opensil/libAMDopensil64.a

quiet_cmd_opensil = OPENSIL $@
      cmd_opensil = $(srctree)/$(src)/opensil/build.sh $(opensil_src) \
	$(abspath $(obj)/opensil) "$(CC)" $(AR) \
	$(abspath $(srctree)/$(src)/opensil/include) \
	$(shell $(CC) -print-file-name=include) 0x7010000 \
	$(CONFIG_SPL_TEXT_BASE) \
	$(shell printf 0x%x $$(($(CONFIG_RESET_VEC_LOC) + 0x10 - $(CONFIG_SPL_TEXT_BASE)))) \
	$(if $(CONFIG_TURIN_OPENSIL_DEBUG),1,0)

$(obj)/opensil/libAMDopensil64.a: FORCE
	$(call cmd,opensil)
endif
endif
endif
