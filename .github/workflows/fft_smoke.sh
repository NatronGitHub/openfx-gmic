#!/bin/bash
# Build a G'MIC command-line interpreter from the sources of a build ('make sources' or CMake:
# CImg.h patched for pocketfft, the standard library of the build), with the FFT of a COMMERCIAL build
# (cimg_use_pocketfft), and render FFT-based filters on a 720x576 image (not a power of two,
# which CImg's own FFT rejects): FFT round trip, Fourier Transform, Bandpass.
# Usage (from the top-level directory, where gmic_<version>.tar.gz is): fft_smoke.sh [<sources dir>]
# <sources dir>: where the G'MIC sources are, "." (default) after make LICENSE=COMMERCIAL, or
# <build>/gmic after a CMake build with -DOFX_LICENSE=COMMERCIAL.
# Uses $CXX (default c++), and pkg-config for zlib.
set -euo pipefail
CXX=${CXX:-c++}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SRCDIR=${1:-.}
ver=$(sed -n 's/^GMICVER=//p' Makefile)
tar --strip-components 2 -C "$WORK" -zxf "gmic_$ver.tar.gz" "gmic-$ver/src/gmic_cli.cpp"
# shellcheck disable=SC2046
$CXX -O1 -w -I"$SRCDIR" -Dgmic_core -Dcimg_display=0 -Dcimg_use_zlib -Dcimg_use_pocketfft $(pkg-config --cflags zlib) \
    -c "$SRCDIR/gmic.cpp" -o "$WORK/gmic.o"
# shellcheck disable=SC2046
$CXX -O1 -w -I"$SRCDIR" -Dcimg_display=0 $(pkg-config --cflags zlib) -c "$WORK/gmic_cli.cpp" -o "$WORK/gmic_cli.o"
# shellcheck disable=SC2046
$CXX "$WORK/gmic.o" "$WORK/gmic_cli.o" -o "$WORK/gmic" $(pkg-config --libs zlib) -lpthread
G="$WORK/gmic"
in="srand 1 720,576,1,3 rand 0,255 b 2"
err=$($G -v -1 $in +fft ifft[-2,-1] k[0,-2] sub[0] [1] abs[0] echo '{0,iM}' 2>&1)
echo "FFT round trip on 720x576: max error $err"
awk -v e="$err" 'BEGIN { exit !(e + 0 == e && e < 1e-3) }' || { echo "Error: FFT round trip failed"; exit 1; }
for f in "fx_fourier_transform 0" "fx_bandpass 0,20,0,0"; do
    out=$($G -v -1 $in $f echo '{0,w}x{0,h}' 2>&1) || { echo "$out"; echo "Error: $f failed"; exit 1; }
    case $out in *rror*) echo "$out"; echo "Error: $f failed"; exit 1;; esac
    echo "$f on 720x576: $out"
done
