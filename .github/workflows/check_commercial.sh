#!/bin/bash
# Check a Linux LICENSE=COMMERCIAL bundle (see license.mk): GMIC.ofx must neither need, define nor
# use FFTW (GPL), the bundle must ship the licence texts that these builds need, and the plug-ins
# that its standard library leaves out (gmic_stdlib_commercial.txt) must not be there.
# Usage: check_commercial.sh <dir containing GMIC.ofx.bundle> <describe log>
# (the log of describe_plugins.sh, with its PLUGIN <id> lines: see DESCRIBE_LOG there)
set -euo pipefail
B="$1/GMIC.ofx.bundle"
log=$2
ofx=$(find "$B/Contents" -name GMIC.ofx | head -1)
[ -f "$ofx" ] || { echo "Error: no GMIC.ofx in $B"; exit 1; }
status=0
readelf -d "$ofx" | grep NEEDED
if readelf -d "$ofx" | grep NEEDED | grep -i fftw; then
    echo "Error: the COMMERCIAL build needs FFTW (see above)"; status=1
fi
if nm -D --undefined-only "$ofx" | grep -i fftw; then
    echo "Error: the COMMERCIAL build uses FFTW symbols (see above)"; status=1
fi
if nm -D --defined-only "$ofx" | grep -i fftw; then
    echo "Error: the COMMERCIAL build defines FFTW symbols (see above)"; status=1
fi
for f in COPYING Licence_CeCILL-C_V1-en.txt Licence_CeCILL_V2-en.txt Licence_CeCILL-B_V1-en.txt \
         Licence_pocketfft.txt; do
    [ -f "$B/Contents/Resources/$f" ] || { echo "Error: $f is not in $B/Contents/Resources"; status=1; }
done
# some plug-ins of left-out community files (GPL v3, CC BY-NC-SA, CeCILL v2 only) and of
# removed filters (gmic_stdlib_commercial.txt)
grep -c '^PLUGIN ' "$log"
grep -qx "PLUGIN eu.gmic.FourierTransform" "$log" || { echo "Error: no PLUGIN lines in $log?"; status=1; }
for id in DreamSmoothingEnhanced DreamSmoothingEnhancedV2 IainStarBurst MoireRemoval SVGSymetriesA \
          ChalkItUpFr VintageStyle; do
    if grep -qx "PLUGIN eu.gmic.$id" "$log"; then
        echo "Error: eu.gmic.$id is in the COMMERCIAL build"; status=1
    fi
done
exit $status
