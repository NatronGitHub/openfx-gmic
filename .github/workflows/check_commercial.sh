#!/bin/bash
# Check a Linux LICENSE=COMMERCIAL bundle (see license.mk): GMIC.ofx must not need FFTW (GPL), nor
# MKL's shared libraries (FFT=mkl links MKL statically), and the bundle must ship the licence
# texts of G'MIC and of the FFT library.
# Usage: check_commercial.sh <dir containing GMIC.ofx.bundle> <mkl|pocketfft>
set -euo pipefail
B="$1/GMIC.ofx.bundle"
fft=$2
ofx="$B/Contents/Linux-x86-64/GMIC.ofx"
[ -f "$ofx" ] || ofx=$(find "$B/Contents" -name GMIC.ofx | head -1)
[ -f "$ofx" ] || { echo "Error: no GMIC.ofx in $B"; exit 1; }
status=0
readelf -d "$ofx" | grep NEEDED
if readelf -d "$ofx" | grep NEEDED | grep -i -E 'fftw|mkl'; then
    echo "Error: the COMMERCIAL build needs FFTW or MKL shared libraries (see above)"; status=1
fi
if nm -D --undefined-only "$ofx" | grep -i -E 'fftw|mkl'; then
    echo "Error: the COMMERCIAL build has undefined FFTW or MKL symbols (see above)"; status=1
fi
case $fft in
mkl) lic=Licence_Intel_Simplified_Software_License.txt;;
pocketfft) lic=Licence_pocketfft.txt;;
*) echo "Error: unknown FFT '$fft'"; exit 1;;
esac
for f in COPYING Licence_CeCILL-C_V1-en.txt Licence_CeCILL_V2-en.txt $lic; do
    [ -f "$B/Contents/Resources/$f" ] || { echo "Error: $f is not in $B/Contents/Resources"; status=1; }
done
exit $status
