#!/bin/bash
# Check the FFT library that a GMIC.ofx.bundle links: pocketfft (the default, header-only) must
# not link FFTW, fftw3 must link libfftw3 and libfftw3_threads.
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
case $fft in
    pocketfft)
        [ -z "$fftw" ] || { echo "Error: $ofx (FFT=pocketfft) links FFTW:" $fftw; exit 1; } ;;
    fftw3)
        if ! echo "$fftw" | grep -q fftw3_threads || ! echo "$fftw" | grep -v fftw3_threads | grep -q fftw3; then
            echo "Error: $ofx (FFT=fftw3) does not link libfftw3 and libfftw3_threads:" $libs; exit 1
        fi ;;
    *) echo "Usage: $0 <GMIC.ofx.bundle> pocketfft|fftw3"; exit 2 ;;
esac
echo "OK: $ofx (FFT=$fft) links:" ${fftw:-no FFTW}
