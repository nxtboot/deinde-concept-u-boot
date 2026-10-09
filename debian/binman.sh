#!/bin/sh
# Run binman for the package build
#
# Binman is maintained as the binary-manager package. Launchpad builds have
# no network access, so debian/update-ppa.sh puts the version which U-Boot
# asks for into debian/binman/ in the source package. Use that, falling back
# to a binman which is already installed.
#
# Binman runs in the build directory. When the build has no python3-libfdt,
# U-Boot builds pylibfdt itself (see debian/rules), so pick that up too.

dir="$(dirname "$(readlink -f "$0")")/binman"
pylibfdt="$PWD/scripts/dtc/pylibfdt"
if [ -d "$pylibfdt" ]; then
	PYTHONPATH="$pylibfdt${PYTHONPATH:+:$PYTHONPATH}"
	export PYTHONPATH
fi

if [ -d "$dir/binman" ]; then
	PYTHONPATH="$dir${PYTHONPATH:+:$PYTHONPATH}" exec python3 -m binman "$@"
fi
if command -v binman >/dev/null 2>&1; then
	exec binman "$@"
fi
echo "binman is missing: run debian/update-ppa.sh or install binary-manager" >&2
exit 1
