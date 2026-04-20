# SPDX-License-Identifier: GPL-2.0+
# Copyright (c) 2011 The Chromium OS Authors.

PLATFORM_CPPFLAGS += -D__SANDBOX__ -U_FORTIFY_SOURCE
PLATFORM_CPPFLAGS += -fPIC -ffunction-sections -fdata-sections

ifeq ($(CONFIG_BACKTRACE),y)
GCC_LIB_DIR := $(shell $(CC) -print-file-name=)
PLATFORM_LIBS += -L$(GCC_LIB_DIR) -lbacktrace
endif
PLATFORM_LIBS += -lrt -lpthread

ifdef GPROF
PLATFORM_LIBS += -pg
endif

SDL_CONFIG ?= sdl2-config

# Define this to avoid linking with SDL, which requires SDL libraries
# This can solve 'sdl-config: Command not found' errors
ifeq ($(CONFIG_SANDBOX_SDL),y)
PLATFORM_LIBS += $(shell $(SDL_CONFIG) --libs)
PLATFORM_CPPFLAGS += $(shell $(SDL_CONFIG) --cflags)
endif

SANITIZERS :=
ifdef CONFIG_ASAN
SANITIZERS	+= -fsanitize=address
# Anything that links libu-boot.a with --whole-archive (ulib tests, examples)
# pulls in AddressSanitizer-instrumented objects, so must also link libasan.
PLATFORM_LIBS	+= -lasan
endif
ifdef CONFIG_FUZZ
SANITIZERS	+= -fsanitize=fuzzer
endif
KBUILD_CFLAGS	+= $(SANITIZERS)

# Avoid defeating linker's garbage collection
ifeq ($(CONFIG_BACKTRACE)$(CONFIG_CMDLINE),yy)
RDYNAMIC += -rdynamic
endif

cmd_u-boot__ = \
	touch $(u-boot-main) ; \
	$(CC) -o $@ -Wl,-T u-boot.lds $(u-boot-init) \
	$(KBUILD_LDFLAGS:%=-Wl,%) \
	$(SANITIZERS) \
	$(LTO_FINAL_LDFLAGS) \
	-Wl,--whole-archive \
		$(u-boot-main) \
		$(u-boot-keep-syms-lto) \
	-Wl,--no-whole-archive \
	$(RDYNAMIC) $(PLATFORM_LIBS) -Wl,-Map -Wl,u-boot.map -Wl,--gc-sections

cmd_u-boot-spl = (cd $(obj) && \
	touch $(patsubst $(obj)/%,%,$(u-boot-spl-main)) && \
	$(CC) -o $(SPL_BIN) -Wl,-T u-boot-spl.lds \
	$(KBUILD_LDFLAGS:%=-Wl,%) \
	$(SANITIZERS) \
	$(LTO_FINAL_LDFLAGS) \
	$(patsubst $(obj)/%,%,$(u-boot-spl-init)) \
	-Wl,--whole-archive \
		$(patsubst $(obj)/%,%,$(u-boot-spl-main)) \
		$(patsubst $(obj)/%,%,$(u-boot-spl-platdata)) \
		$(patsubst $(obj)/%,%,$(u-boot-spl-keep-syms-lto)) \
	-Wl,--no-whole-archive \
	$(PLATFORM_LIBS) -Wl,-Map -Wl,u-boot-spl.map -Wl,--gc-sections)

ifeq ($(HOST_ARCH),$(HOST_ARCH_X86_64))
EFI_LDS := ${SRCDIR}/../../../arch/x86/lib/elf_x86_64_efi.lds
EFI_TARGET := --output-target=efi-app-x86_64
else ifeq ($(HOST_ARCH),$(HOST_ARCH_X86))
EFI_LDS := ${SRCDIR}/../../../arch/x86/lib/elf_ia32_efi.lds
EFI_TARGET := --output-target=efi-app-ia32
else ifeq ($(HOST_ARCH),$(HOST_ARCH_AARCH64))
EFI_LDS := ${SRCDIR}/../../../arch/arm/lib/elf_aarch64_efi.lds
OBJCOPYFLAGS += -j .text -j .secure_text -j .secure_data -j .rodata -j .data \
		-j __u_boot_list -j .rela.dyn -j .got -j .got.plt \
		-j .binman_sym_table -j .text_rest \
		-j .efi_runtime -j .efi_runtime_rel
else ifeq ($(HOST_ARCH),$(HOST_ARCH_ARM))
EFI_LDS := ${SRCDIR}/../../../arch/arm/lib/elf_arm_efi.lds
OBJCOPYFLAGS += -j .text -j .secure_text -j .secure_data -j .rodata -j .hash \
		-j .data -j .got -j .got.plt -j __u_boot_list -j .rel.dyn \
		-j .binman_sym_table -j .text_rest \
		-j .efi_runtime -j .efi_runtime_rel
else ifeq ($(HOST_ARCH),$(HOST_ARCH_RISCV32))
EFI_LDS := ${SRCDIR}/../../../arch/riscv/lib/elf_riscv32_efi.lds
else ifeq ($(HOST_ARCH),$(HOST_ARCH_RISCV64))
EFI_LDS := ${SRCDIR}/../../../arch/riscv/lib/elf_riscv64_efi.lds
endif
EFI_CRT0 := crt0_sandbox_efi.o
EFI_RELOC := reloc_sandbox_efi.o

# U-Boot Library
LIB_LDS := $(srctree)/arch/sandbox/cpu/u-boot-lib.lds
LIB_STATIC_LDS := $(srctree)/arch/sandbox/cpu/ulib-test-static.lds
