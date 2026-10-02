#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Build AMD's openSIL library with U-Boot's compiler
#
# Copyright 2026 Simon Glass <sjg@chromium.org>
#
# openSIL builds with meson and takes its platform settings from its own
# Kconfig. This writes the settings and a meson cross file for U-Boot's
# compiler and flags, then builds the 64-bit library. It does not write to the
# openSIL tree. Meson only rebuilds what has changed.
#
# Usage: build.sh <opensil-dir> <build-dir> <cc> <ar> <port-dir> <gcc-include>
#	<apob-base> <bios-base> <bios-size> <debug>

set -e

src=$1
out=$2
cc=$3
ar=$4
port=$5
gccinc=$6
apob=$7
bios_base=$8
bios_size=$9
shift 9
debug=$1

if [ ! -f "${src}/meson.build" ]; then
	echo "openSIL tree not found at '${src}' (CONFIG_TURIN_OPENSIL_PATH)" >&2
	exit 1
fi

mkdir -p "${out}"

# The platform settings, as Dasharo's coreboot gives them for Turin
cat > "${out}/opensil_config.new" <<CFG
CONFIG_PLAT_APOB_ADDRESS=${apob}
CONFIG_PSP_BIOS_BIN_BASE=${bios_base}
CONFIG_PSP_BIOS_BIN_SIZE=${bios_size}
CONFIG_PLAT_NUMBER_SOCKETS=1
CONFIG_SOC_F1AM00=y
CFG
if ! cmp -s "${out}/opensil_config.new" "${out}/opensil_config.in"; then
	mv "${out}/opensil_config.new" "${out}/opensil_config.in"
	cp "${out}/opensil_config.in" "${out}/opensil_config"
	# U-Boot's build exports the srctree variable, which Kconfig would
	# otherwise use
	(cd "${src}" && PYTHONDONTWRITEBYTECODE=1 srctree="${src}" \
		KCONFIG_CONFIG="${out}/opensil_config" \
		KCONFIG_AUTOHEADER="${out}/opensil_config.h" \
		python3 util/kconfig/lib/genconfig.py \
			--config-out "${out}/opensil_config" Kconfig)
else
	rm "${out}/opensil_config.new"
fi

# Meson drops any -isystem option, so the compiler's own headers go on a
# plain -I option instead. U-Boot's code is position-independent and must
# not use SSE. openSIL asks for the large code model, whose
# position-independent code reaches its globals through a GOT laid out as
# U-Boot's link does not; the small model, as U-Boot uses, reaches them
# relative to the code, and the cross file's flags come after openSIL's
# own. openSIL's -Werror option trips on one maybe-uninitialized warning in
# its fabric code, which is a false positive
cat > "${out}/cross.ini.new" <<CROSS
[binaries]
c = [$(printf "'%s', " ${cc} | sed 's/, $//')]
ar = '${ar}'
nasm = 'nasm'

[built-in options]
c_args = ['-nostdinc', '-I${gccinc}', '-I${port}',
	'-m64', '-march=core2', '-mno-mmx', '-mno-sse', '-fpic',
	'-mcmodel=small',
	'-fvisibility=hidden', '-ffreestanding', '-fno-builtin',
	'-fno-stack-protector',
	'-DHAS_STRING_H=1', '-DSIL_DEBUG_ENABLE=${debug}',
	'-Wno-unused-parameter', '-Wno-missing-field-initializers',
	'-Wno-unused-but-set-variable', '-Wno-error=maybe-uninitialized']

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
is32bit = false
CROSS
if ! cmp -s "${out}/cross.ini.new" "${out}/cross.ini"; then
	mv "${out}/cross.ini.new" "${out}/cross.ini"
	rm -rf "${out}/meson"
else
	rm "${out}/cross.ini.new"
fi

if [ ! -f "${out}/meson/build.ninja" ]; then
	meson setup --cross-file "${out}/cross.ini" --buildtype=minsize \
		-DPlatKcfgDir="${out}" -DPlatKcfg=opensil_config \
		"${out}/meson" "${src}" >"${out}/meson-setup.log"
fi
meson compile -C "${out}/meson" AMDopensil64 >"${out}/meson-compile.log"

# Only touch the library when it changes, so that U-Boot is not relinked
if ! cmp -s "${out}/meson/libAMDopensil64.a" "${out}/libAMDopensil64.a"; then
	cp "${out}/meson/libAMDopensil64.a" "${out}/libAMDopensil64.a"
fi
