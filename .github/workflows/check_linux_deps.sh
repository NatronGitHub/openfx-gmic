#!/bin/bash
# Check that a Linux GMIC.ofx needs only system libraries, and exports only the OFX entry points:
# - its NEEDED entries are all in the allowed set: libc, libm, libstdc++, libgcc_s, libgomp
#   (OpenMP), zlib (a system library, though GMIC.ofx links it statically), and libpthread,
#   libdl, librt and ld-linux, which are separate from libc only before glibc 2.34 (older systems
#   than Natron's Rocky Linux 9 baseline); libpng and FFTW are linked statically;
# - no libcurl (G'MIC downloads with the curl or wget command), checked through the NEEDED list;
# - it exports only OfxGetNumberOfPlugins and OfxGetPlugin (the version script keeps the symbols
#   of the static libraries local).
# It also prints the highest GLIBC_, GLIBCXX_, CXXABI_, GCC_ and GOMP_ symbol versions required,
# which must exist on the oldest supported system (Natron 2.6: Rocky Linux 9, glibc 2.34 and
# GCC 11's libstdc++, GLIBCXX_3.4.29 and CXXABI_1.3.13).
# MAX_GLIBC=<version> (e.g. 2.34), MAX_GLIBCXX=<version> (e.g. 3.4.29): fail if a newer GLIBC_ or
# GLIBCXX_ version is required.
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
        libc.so.6|libm.so.6|libstdc++.so.6|libgcc_s.so.1|libgomp.so.1|libz.so.1) ;;
        libpthread.so.0|libdl.so.2|librt.so.1|ld-linux*.so*) ;; # glibc < 2.34
        *) echo "FAIL: $bin needs $lib, which is not a system library (link it statically)"; status=1 ;;
    esac
done

exports=$(nm -D --defined-only "$bin" | awk '{ print $NF }' | LC_ALL=C sort | tr '\n' ' ')
echo "Exported symbols: $exports"
if [ "$exports" != "OfxGetNumberOfPlugins OfxGetPlugin " ]; then
    echo "FAIL: $bin exports other symbols than OfxGetNumberOfPlugins and OfxGetPlugin"
    status=1
fi

vsort() { sort -t. -k1,1n -k2,2n -k3,3n; }
# the versions that the binary requires (dynamic symbol table)
versions=$(objdump -T "$bin" | grep -o -E '\b(GLIBC|GLIBCXX|CXXABI|GCC|GOMP|OMP)_[0-9][0-9.]*' | LC_ALL=C sort -u)
for prefix in GLIBC GLIBCXX CXXABI GCC GOMP OMP; do
    v=$(echo "$versions" | sed -n -e "s/^${prefix}_//p" | vsort | tail -n 1)
    [ -z "$v" ] && continue
    echo "Highest ${prefix}_ version required: ${prefix}_$v"
    case "$prefix" in GLIBC) max=${MAX_GLIBC:-} ;; GLIBCXX) max=${MAX_GLIBCXX:-} ;; *) max= ;; esac
    if [ -n "$max" ] && [ "$(printf '%s\n%s\n' "$v" "$max" | vsort | tail -n 1)" != "$max" ]; then
        echo "FAIL: ${prefix}_$v is newer than ${prefix}_$max; symbols that need it:"
        objdump -T "$bin" | grep -E "\b${prefix}_$v\b" | awk '{ print "  " $NF }' | head -n 10 || true
        status=1
    fi
done

[ $status = 0 ] && echo "OK: $bin needs only system libraries"
exit $status
