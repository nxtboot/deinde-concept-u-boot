# SPDX-License-Identifier: GPL-2.0

# U-Boot interface and support
obj-y := interface.o support.o

# VFS layer integration
obj-$(CONFIG_$(PHASE_)VFS) += fs.o

# Core Linux isofs files (kept close to upstream)
obj-y += namei.o inode.o dir.o util.o rock.o export.o

# Optional extensions
obj-$(CONFIG_JOLIET) += joliet.o
