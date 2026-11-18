#!/bin/bash
# Check the FFT library that a GMIC.ofx.bundle links: pocketfft (the default, header-only) must
# not link FFTW, fftw3 must link libfftw3 and libfftw3_threads. On Linux, FFTW is linked statically
# (GMIC_STATIC_DEPS): then its (unstripped) symbol table must define fftw_execute (libfftw3.a) and
# fftw_plan_with_nthreads (libfftw3_threads.a), with no undefined FFTW symbol.
# Usage: check_fft.sh <GMIC.ofx.bundle> pocketfft|fftw3
set -euo pipefail
bundle=$1 fft=$2
ofx=$(find "$bundle/Contents" -type f -name GMIC.ofx | head -1)
[ -n "$ofx" ] || { echo "Error: no GMIC.ofx in $bundle"; exit 1; }
case $(uname -s) in
    Darwin) libs=$(otool -L "$ofx" | tail -n +2 | awk '{print $1}') ;;
    Linux) libs=$(readelf -d "$ofx" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p') ;;
    *) libs=$(objdump -p "$ofx" | sed -n 's/.*DLL Name: //p') ;;
esac
fftw=$(echo "$libs" | grep -i fftw || true)
static=""
if [ "$(uname -s)" = Linux ] && [ -z "$fftw" ]; then
    undefined=$(nm -D --undefined-only "$ofx" | grep -i fftw || true)
    [ -z "$undefined" ] || { echo "Error: $ofx has undefined FFTW symbols:" $undefined; exit 1; }
    # the (unstripped) symbol table: FFTW's code, local, from libfftw3.a and libfftw3_threads.a
    defined=$(nm "$ofx" 2>/dev/null | awk '$2 ~ /^[tT]$/ && $3 ~ /^fftw_(execute|plan_with_nthreads)$/ { print $3 }' | LC_ALL=C sort -u | tr '\n' ' ')
    case "$defined" in
        "fftw_execute fftw_plan_with_nthreads ") static="FFTW (static: libfftw3.a, libfftw3_threads.a)" ;;
        ?*) static="FFTW (static, incomplete: $defined)" ;;
    esac
fi
case $fft in
    pocketfft)
        [ -z "$fftw$static" ] || { echo "Error: $ofx (FFT=pocketfft) links FFTW:" $fftw $static; exit 1; } ;;
    fftw3)
        if [ "${static#FFTW (static:}" = "$static" ] && { ! echo "$fftw" | grep -q fftw3_threads || ! echo "$fftw" | grep -v fftw3_threads | grep -q fftw3; }; then
            echo "Error: $ofx (FFT=fftw3) does not link libfftw3 and libfftw3_threads:" $libs; exit 1
        fi ;;
    *) echo "Usage: $0 <GMIC.ofx.bundle> pocketfft|fftw3"; exit 2 ;;
esac
fftw="$fftw$static"
echo "OK: $ofx (FFT=$fft) links:" ${fftw:-no FFTW}
