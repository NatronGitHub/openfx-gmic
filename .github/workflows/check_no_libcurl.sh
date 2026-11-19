#!/bin/bash
# Check that a macOS or Windows GMIC.ofx.bundle doesn't use libcurl (Linux: check_linux_deps.sh).
# G'MIC downloads filter data with the system's curl command (see gmic_plugin_call.cpp): GMIC.ofx
# must not link libcurl (macOS: otool -L, Windows: objdump -p), and the bundle must not contain a
# libcurl library (macOS Contents/Libraries, Windows Contents/Win64, after osxDeploy.sh or
# bundle_libs.sh).
# Usage: check_no_libcurl.sh <GMIC.ofx.bundle>
set -euo pipefail
bundle=${1:?usage: $0 <GMIC.ofx.bundle>}
ofx=$(find "$bundle/Contents" -type f -name GMIC.ofx | head -1)
[ -n "$ofx" ] || { echo "Error: no GMIC.ofx in $bundle"; exit 1; }
case $(uname -s) in
    Darwin) libs=$(otool -L "$ofx" | tail -n +2 | awk '{print $1}') ;;
    *) libs=$(objdump -p "$ofx" | sed -n 's/.*DLL Name: //p') ;;
esac
status=0
if echo "$libs" | grep -i curl; then
    echo "Error: $ofx links libcurl (see above)"; status=1
fi
if find "$bundle/Contents" -iname '*curl*' | grep .; then
    echo "Error: $bundle contains libcurl (see above)"; status=1
fi
[ $status = 0 ] && echo "OK: $ofx doesn't use libcurl; it links:" $libs
exit $status
