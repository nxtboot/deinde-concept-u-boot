# SPDX-License-Identifier: GPL-2.0+
#
# Makefile for U-Boot Rust example
#
# Copyright 2025 Canonical Ltd.
# Written by Simon Glass <simon.glass@canonical.com>

# This Makefile integrates with the U-Boot build system, similar to
# examples/ulib
#
# Usage: cd examples/rust; make UBOOT_BUILD=/tmp/b/sandbox srctree=../..
#

# Default paths - can be overridden
UBOOT_BUILD ?= /tmp/b/sandbox
srctree ?= ../..
OUTDIR ?= .

# Export for build.rs
export UBOOT_BUILD
export UBOOT_ASAN
export UBOOT_COVERAGE

# Common source dependencies
RUST_SOURCES = Cargo.toml src/demo.rs build.rs

# Absolute output directory for Cargo builds
OUTDIR_ABS = $(abspath $(OUTDIR))

# Ensure 'cargo' is available
CARGO := $(shell command -v cargo 2> /dev/null)
ifeq ($(CARGO),)
$(error "Cargo not found. Please install Rust toolchain from \
https://rustup.rs/")
endif

# Default target - build both static and dynamic versions like examples_ulib
all: $(OUTDIR)/demo $(OUTDIR)/demo_static

# Create output directory
$(OUTDIR):
	@mkdir -p $(OUTDIR)

# Build dynamic version (links with libu-boot.so)
$(OUTDIR)/demo: $(RUST_SOURCES) $(UBOOT_BUILD)/libu-boot.so | $(OUTDIR)
	@if [ ! -f "$(UBOOT_BUILD)/libu-boot.so" ]; then \
		echo "No shared library at $(UBOOT_BUILD)/libu-boot.so" >&2; \
		echo "Please build U-Boot: make sandbox_defconfig && make" >&2; \
		exit 1; \
	fi
	@UBOOT_DYNAMIC=1 $(CARGO) build --target-dir \
		$(OUTDIR_ABS)/build-dynamic --release --bin demo -q
	@cp $(OUTDIR_ABS)/build-dynamic/release/demo $(OUTDIR)/

# Build static version (links with libu-boot.a)
$(OUTDIR)/demo_static: $(RUST_SOURCES) $(UBOOT_BUILD)/libu-boot.a | $(OUTDIR)
	@if [ ! -f "$(UBOOT_BUILD)/libu-boot.a" ]; then \
		echo "No static library at $(UBOOT_BUILD)/libu-boot.a" >&2; \
		echo "Please build U-Boot: make sandbox_defconfig && make" >&2; \
		exit 1; \
	fi
	@$(CARGO) build --target-dir $(OUTDIR_ABS)/build-static --release \
		--bin demo -q
	@cp $(OUTDIR_ABS)/build-static/release/demo $(OUTDIR)/demo_static

demo: $(OUTDIR)/demo

# Test the programs
test: $(OUTDIR)/demo $(OUTDIR)/demo_static
	@echo "Testing Rust demos..."
	@echo "Testing dynamic version:"
	@LD_LIBRARY_PATH="$(UBOOT_BUILD)" $(OUTDIR)/demo
	@echo "Testing static version:"
	@$(OUTDIR)/demo_static

# Clean build artifacts
clean:
	$(CARGO) clean
	@if [ "$(OUTDIR)" != "." ]; then \
		rm -rf $(OUTDIR_ABS)/build-dynamic \
			$(OUTDIR_ABS)/build-static $(OUTDIR)/demo \
			$(OUTDIR)/demo_static; \
	else \
		rm -f demo demo_static; \
	fi

# Show cargo version and info
info:
	@echo "Rust toolchain information:"
	$(CARGO) --version
	@echo "U-Boot build directory: $(UBOOT_BUILD)"
	@echo "U-Boot source tree: $(srctree)"

.PHONY: all test clean info
