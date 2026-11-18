#!/bin/bash
# Check that a Linux GMIC.ofx needs only system libraries, and exports only the OFX entry points:
# - its NEEDED entries are all in the allowed set: libc, libm, ld-linux (glibc's dynamic loader),
#   libstdc++, libgcc_s, libgomp (OpenMP), and libpthread, libdl and librt, which are separate
#   from libc only before glibc 2.34 (older systems than Natron's Rocky Linux 9 baseline); zlib,
#   libpng and FFTW are linked statically (a shared libz.so.1 fails);
# - no libcurl (G'MIC downloads with the curl or wget command), checked through the NEEDED list;
# - it exports only OfxGetNumberOfPlugins and OfxGetPlugin (the version script keeps the symbols
#   of the static libraries local).
# It also reports the highest GLIBC_, GLIBCXX_, CXXABI_, GCC_ and GOMP_ symbol versions required,
# which must exist on the oldest supported system (Natron 2.6: Rocky Linux 9, glibc 2.34 and
# GCC 11's libstdc++, GLIBCXX_3.4.29 and CXXABI_1.3.13): a build there can only require those.
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
        libc.so.6|libm.so.6|ld-linux*.so*|libstdc++.so.6|libgcc_s.so.1|libgomp.so.1) ;;
        libpthread.so.0|libdl.so.2|librt.so.1) ;; # glibc < 2.34
        *) echo "FAIL: $bin needs $lib, which is not a system library (link it statically)"; status=1 ;;
    esac
done

# (without the version node, type A, and the @@OFX_1.0 version suffixes of the version script)
exports=$(nm -D --defined-only "$bin" | awk '$(NF-1) != "A" { sub(/@.*/, "", $NF); print $NF }' | LC_ALL=C sort | tr '\n' ' ')
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
done

[ $status = 0 ] && echo "OK: $bin needs only system libraries"
exit $status
