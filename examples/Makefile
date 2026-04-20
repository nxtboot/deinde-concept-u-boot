# SPDX-License-Identifier: GPL-2.0+

ifdef CONFIG_EXAMPLES

ifdef FTRACE
subdir-ccflags-y += -finstrument-functions -DFTRACE
endif

subdir-$(EXAMPLES_STANDALONE) += standalone
# examples/legacy_api/crt0.S is bare-metal only; skip on sandbox.
ifndef CONFIG_SANDBOX
subdir-$(CONFIG_LEGACY_API) += legacy_api
subdir-$(CONFIG_ULIB) += ulib
endif
endif
