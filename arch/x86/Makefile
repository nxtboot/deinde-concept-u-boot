# SPDX-License-Identifier: GPL-2.0+

ifeq ($(CONFIG_EFI_APP),)
ifdef CONFIG_$(PHASE_)X86_64
ifneq ($(CONFIG_$(PHASE_)X86_16BIT_INIT)$(CONFIG_X86_32BIT_ENTRY),)
head-y := arch/x86/cpu/start_from_32.o
else
head-y := arch/x86/cpu/start64.o
endif
else
ifeq ($(CONFIG_$(PHASE_)X86_16BIT_INIT),y)
head-y := arch/x86/cpu/start.o
else
ifndef CONFIG_SPL
head-y := arch/x86/cpu/start.o
else
ifdef CONFIG_XPL_BUILD
head-y	= arch/x86/cpu/start_from_tpl.o
else
head-y	= arch/x86/cpu/start_from_spl.o
endif
endif
endif
endif
endif # EFI

ifndef CONFIG_$(PHASE_)X86_64
head-$(CONFIG_$(PHASE_)X86_16BIT_INIT) += arch/x86/cpu/start16.o
head-$(CONFIG_$(PHASE_)X86_16BIT_INIT) += arch/x86/cpu/resetvec.o
endif

libs-y += arch/x86/cpu/
libs-y += arch/x86/lib/

OBJCOPYFLAGS_u-boot-x86-start16.bin := -O binary -j .start16
OBJCOPYFLAGS_u-boot-x86-reset16.bin := -O binary -j .resetvec

ifneq ($(CONFIG_X86_64)$(CONFIG_X86_16BIT_INIT),yy)
# Normal case: extract 16-bit sections from the u-boot ELF
u-boot-x86-start16.bin: u-boot FORCE
	$(call if_changed,objcopy)

u-boot-x86-reset16.bin: u-boot FORCE
	$(call if_changed,objcopy)
else
# 64-bit with 16-bit init: the 16-bit objects are built as 32-bit and
# not linked into the 64-bit u-boot.  Link them into a small 32-bit
# ELF with the correct addresses so that cross-references (e.g.,
# resetvec -> start16 -> _start) are resolved.
u-boot-x86-16bit.lds: $(srctree)/arch/x86/cpu/u-boot-16bit.lds prepare FORCE
	$(call if_changed_dep,cpp_lds)

LDFLAGS_u-boot-x86-16bit.elf := -m elf_i386 -static -nostdlib \
	--defsym=_start=$(CONFIG_TEXT_BASE)

u-boot-x86-16bit.elf: arch/x86/cpu/start16.o arch/x86/cpu/resetvec.o \
		u-boot-x86-16bit.lds FORCE
	$(LD) $(LDFLAGS_u-boot-x86-16bit.elf) -T u-boot-x86-16bit.lds \
		arch/x86/cpu/start16.o arch/x86/cpu/resetvec.o -o $@

u-boot-x86-start16.bin: u-boot-x86-16bit.elf FORCE
	$(call if_changed,objcopy)

u-boot-x86-reset16.bin: u-boot-x86-16bit.elf FORCE
	$(call if_changed,objcopy)
endif

ifdef CONFIG_EXAMPLES
EXAMPLE_ARCH := x86
include scripts/Makefile.ulib-example
endif
