#!/bin/bash
# Check that a macOS GMIC.ofx requires macOS 14.0 (its minos, LC_BUILD_VERSION: CMakeLists.txt and
# openfx's Makefile.master default to 14.0, unless MACOSX_DEPLOYMENT_TARGET is set), and report the
# minos of the dylibs bundled in Contents/Libraries: MacPorts builds them for the build machine's
# macOS version, which may be newer (a known limitation, not checked).
# Usage: check_macos_minos.sh <GMIC.ofx.bundle>
set -euo pipefail

minos() { otool -l "$1" | awk '$1 == "cmd" { b = ($2 == "LC_BUILD_VERSION") } b && $1 == "minos" { print $2; exit }'; }
bin=$1/Contents/MacOS/GMIC.ofx
v=$(minos "$bin")
echo "GMIC.ofx: minos ${v:-none}"
for f in "$1"/Contents/Libraries/*.dylib; do
    [ -f "$f" ] && echo "$(basename "$f"): minos $(minos "$f")"
done
expected=${MACOSX_DEPLOYMENT_TARGET:-14.0}
if [ "$v" != "$expected" ]; then
    echo "FAIL: $bin requires macOS ${v:-?}, not $expected"
    exit 1
fi
