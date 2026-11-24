#!/bin/bash
# Check that a macOS GMIC.ofx requires exactly MACOSX_DEPLOYMENT_TARGET, or 14.0 if it is unset (its
# minos, LC_BUILD_VERSION: CMakeLists.txt and openfx's Makefile.master default to 14.0; CI sets the
# runner's macOS), and report the minos of the dylibs bundled in Contents/Libraries: MacPorts
# builds them for the build machine's macOS, so they match in CI but may be newer elsewhere (not
# checked).
# Usage: check_macos_minos.sh <GMIC.ofx.bundle>
set -euo pipefail

if [ $# != 1 ]; then
    echo "Usage: $0 <GMIC.ofx.bundle>"
    exit 1
fi
# (otool's whole output first: awk exiting early would make otool fail with SIGPIPE under pipefail)
minos() {
    local l
    l=$(otool -l "$1")
    awk '$1 == "cmd" { b = ($2 == "LC_BUILD_VERSION") } b && $1 == "minos" { print $2; exit }' <<< "$l"
}
# a version as a number to compare: 14 == 14.0 == 14.0.0
vnum() { awk -F. '{ printf "%d%03d%03d\n", $1, $2, $3 }' <<< "$1"; }
bin=$1/Contents/MacOS/GMIC.ofx
v=$(minos "$bin")
echo "GMIC.ofx: minos ${v:-none}"
for f in "$1"/Contents/Libraries/*.dylib; do
    [ -f "$f" ] && echo "$(basename "$f"): minos $(minos "$f")"
done
expected=${MACOSX_DEPLOYMENT_TARGET:-14.0}
if [ -z "$v" ] || [ "$(vnum "$v")" != "$(vnum "$expected")" ]; then
    echo "FAIL: $bin requires macOS ${v:-?}, not $expected"
    exit 1
fi
