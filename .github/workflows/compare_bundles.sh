#!/bin/bash
# Compare two GMIC.ofx.bundle directories, e.g. the Makefile's and the CMake install tree's:
# - the same files (names, including Contents/Libraries on macOS release builds), except for the
#   Contents/_CodeSignature of an ad hoc signature (the CMake install step signs the bundle);
# - the same contents, except for the plug-in binary and the bundled libraries;
# - the same exported symbols of the plug-in binary;
# - the same direct dependencies (library names), OpenGL included: both builds link it for the
#   point handles (openfx-supportext).
# Usage: compare_bundles.sh <GMIC.ofx.bundle> <GMIC.ofx.bundle>
set -euo pipefail

if [ $# != 2 ]; then
    echo "Usage: $0 <GMIC.ofx.bundle> <GMIC.ofx.bundle>"
    exit 1
fi
A=$(cd "$1" && pwd)
B=$(cd "$2" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
status=0

files() { (cd "$1" && find . \( -type f -o -type l \) ! -path './Contents/_CodeSignature/*' | LC_ALL=C sort); }
files "$A" > "$WORK/files.a"
files "$B" > "$WORK/files.b"
if ! diff -u "$WORK/files.a" "$WORK/files.b"; then
    echo "FAIL: the bundles have different files (see above)"
    status=1
fi

binary=""
while read -r f; do
    case "$f" in
        ./Contents/Libraries/*) ;;
        ./Contents/*/GMIC.ofx) binary="$f" ;;
        *) if [ -f "$B/$f" ] && ! cmp -s "$A/$f" "$B/$f"; then
               echo "FAIL: $f differs"
               status=1
           fi ;;
    esac
done < "$WORK/files.a"
if [ -z "$binary" ] || [ ! -f "$B/$binary" ]; then
    echo "FAIL: no Contents/<arch>/GMIC.ofx in both bundles"
    exit 1
fi
echo "Plug-in binary: $binary"

case "$(uname -s)" in
    Darwin)
        symbols() { nm -gU "$1" | awk '{ print $NF }'; }
        deps() { otool -L "$1" | tail -n +2 | awk '{ print $1 }' | sed -e 's|.*/||'; } ;;
    Linux|FreeBSD)
        symbols() { nm -D --defined-only "$1" | awk '{ print $NF }'; }
        deps() { readelf -d "$1" | sed -n -e 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; } ;;
    *) # MinGW
        symbols() { objdump -p "$1" | sed -n -e '/^\[Ordinal\/Name Pointer\] Table/,/^$/p' | awk 'NF > 1 { print $NF }'; }
        deps() { objdump -p "$1" | sed -n -e 's/^[[:space:]]*DLL Name: //p'; } ;;
esac

# A tool failure (set -e, pipefail) or an empty list is an error: two empty lists would "match".
list() { # list <symbols|deps> <bundle> <output>
    "$1" "$2/$binary" > "$3.raw"
    LC_ALL=C sort "$3.raw" > "$3"
    if [ ! -s "$3" ]; then
        echo "FAIL: no $1 found in $2/$binary"
        exit 1
    fi
}

list symbols "$A" "$WORK/symbols.a"
list symbols "$B" "$WORK/symbols.b"
echo "Exported symbols: $(tr '\n' ' ' < "$WORK/symbols.a")"
if ! diff -u "$WORK/symbols.a" "$WORK/symbols.b"; then
    echo "FAIL: the plug-ins export different symbols (see above)"
    status=1
fi

list deps "$A" "$WORK/deps.a"
list deps "$B" "$WORK/deps.b"
echo "Dependencies: $(tr '\n' ' ' < "$WORK/deps.a")"
if ! diff -u "$WORK/deps.a" "$WORK/deps.b"; then
    echo "FAIL: the plug-ins link different libraries (see above)"
    status=1
fi

[ $status = 0 ] && echo "OK: the bundles match"
exit $status
