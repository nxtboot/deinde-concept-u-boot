# SPDX-License-Identifier: GPL-2.0+
#
# Copyright 2026 Simon Glass <sjg@chromium.org>

# Link openSIL after U-Boot's own objects, so that only what is used comes
# in. It is for U-Boot proper only, since SPL is 32-bit, but SPL's make
# inherits PLATFORM_LIBS from U-Boot's, so drop it there
ifdef CONFIG_TURIN_OPENSIL
ifdef CONFIG_XPL_BUILD
PLATFORM_LIBS := $(filter-out %/libAMDopensil64.a,$(PLATFORM_LIBS))
else ifneq ($(wildcard $(abspath $(CONFIG_TURIN_OPENSIL_PATH:"%"=%))/meson.build),)
PLATFORM_LIBS += $(objtree)/arch/x86/cpu/turin/opensil/libAMDopensil64.a
endif
endif
