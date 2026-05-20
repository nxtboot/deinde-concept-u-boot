# SPDX-License-Identifier: GPL-2.0+
#
# Copyright (c) 2011 The Chromium OS Authors.
#
# (C) Copyright 2000-2003
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

obj-y	:= start.o cache.o cpu.o mem.o state.o os.o tty.o initjmp.o
ifdef CONFIG_FUZZ
obj-y	+= fuzz.o
else
obj-y	+= main.o
endif
obj-$(CONFIG_SANDBOX_SDL)    += sdl.o
obj-$(CONFIG_XPL_BUILD)	+= spl.o
obj-$(CONFIG_ETH_SANDBOX_RAW)	+= eth-raw-os.o
obj-$(CONFIG_BACKTRACE)	+= backtrace.o

# Compile these files with system headers
CFLAGS_USE_SYSHDRS := backtrace.o eth-raw-os.o fuzz.o main.o os.o sdl.o tty.o initjmp.o

# backtrace.c needs libbacktrace header from GCC
LIBBT_INC := $(dir $(shell $(CC) -print-file-name=include/backtrace.h))
CFLAGS_backtrace.o += -isystem $(LIBBT_INC)

# initjmp.c is build in the system environment, so needs standard includes
# CFLAGS_REMOVE_initjmp.o cannot be used to drop header include path
quiet_cmd_cc_initjmp.o = CC $(quiet_modtag)  $@
cmd_cc_initjmp.o = $(CC) $(filter-out -nostdinc, \
	$(patsubst -I%,-idirafter%,$(c_flags))) -c -o $@ $<

$(obj)/initjmp.o: $(src)/initjmp.c FORCE
	$(call if_changed_dep,cc_initjmp.o)

# sdl.c fails to build with -fshort-wchar using musl
cmd_cc_sdl.o = $(CC) $(filter-out -nostdinc -fshort-wchar, \
	$(patsubst -I%,-idirafter%,$(c_flags))) -fno-lto -c -o $@ $<

$(obj)/sdl.o: $(src)/sdl.c FORCE
	$(call if_changed_dep,cc_sdl.o)
