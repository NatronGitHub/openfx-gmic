#!/bin/bash
# Check that a Linux GMIC.ofx needs only system libraries, and exports only the OFX entry points:
# - its NEEDED entries are all in the allowed set: the glibc libraries (libc, libm, libpthread,
#   libdl, librt, ld-linux, separate from libc before glibc 2.34, e.g. on Rocky Linux 8),
#   libstdc++, libgcc_s and libgomp (OpenMP); zlib, libpng and FFTW are linked statically;
# - no libcurl (G'MIC downloads with the curl or wget command), checked through the NEEDED list;
# - it exports only OfxGetNumberOfPlugins and OfxGetPlugin (the version script keeps the symbols
#   of the static libraries local).
# It also prints the highest GLIBC_, GLIBCXX_, CXXABI_, GCC_ and GOMP_ symbol versions required,
# which must exist on the oldest supported system (Natron's releases: Rocky Linux 8, glibc 2.28).
# MAX_GLIBC=<version> (e.g. 2.28): fail if a newer GLIBC_ version is required.
# Usage: check_linux_deps.sh <GMIC.ofx.bundle>
set -euo pipefail

if [ $# != 1 ]; then
    echo "Usage: $0 <GMIC.ofx.bundle>"
    exit 1
fi
bin=$(ls "$1"/Contents/Linux-*/GMIC.ofx)
status=0

needed=$(readelf -d "$bin" | sed -n -e 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
if [ -z "$needed" ]; then
    echo "FAIL: no NEEDED entries in $bin"
    exit 1
fi
echo "NEEDED: $(echo $needed)"
for lib in $needed; do
    case "$lib" in
        libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|librt.so.1|ld-linux*.so*) ;;
        libstdc++.so.6|libgcc_s.so.1|libgomp.so.1) ;;
        *) echo "FAIL: $bin needs $lib, which is not a system library (link it statically)"; status=1 ;;
    esac
done

exports=$(nm -D --defined-only "$bin" | awk '{ print $NF }' | LC_ALL=C sort | tr '\n' ' ')
echo "Exported symbols: $exports"
if [ "$exports" != "OfxGetNumberOfPlugins OfxGetPlugin " ]; then
    echo "FAIL: $bin exports other symbols than OfxGetNumberOfPlugins and OfxGetPlugin"
    status=1
fi

# the highest version of each prefix, among the versions that the binary requires
versions=$(objdump -T "$bin" | grep -o -E '\b(GLIBC|GLIBCXX|CXXABI|GCC|GOMP|OMP)_[0-9][0-9.]*' | LC_ALL=C sort -u)
for prefix in GLIBC GLIBCXX CXXABI GCC GOMP OMP; do
    v=$(echo "$versions" | sed -n -e "s/^${prefix}_//p" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)
    [ -n "$v" ] && echo "Highest ${prefix}_ version required: ${prefix}_$v"
    if [ "$prefix" = GLIBC ] && [ -n "${MAX_GLIBC:-}" ] && [ -n "$v" ] &&
       [ "$(printf '%s\n%s\n' "$v" "$MAX_GLIBC" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)" != "$MAX_GLIBC" ]; then
        echo "FAIL: GLIBC_$v is newer than MAX_GLIBC=$MAX_GLIBC"
        status=1
    fi
done
echo "Symbols requiring the highest GLIBC_ version:"
objdump -T "$bin" | grep -E "GLIBC_$(echo "$versions" | sed -n -e 's/^GLIBC_//p' | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)\b" | awk '{ print "  " $NF }' | head -n 10 || true

[ $status = 0 ] && echo "OK: $bin needs only system libraries"
exit $status
