#!/bin/bash
# Check that a macOS or Windows GMIC.ofx.bundle doesn't use libcurl (Linux: check_linux_deps.sh).
# G'MIC downloads filter data with the system's curl command (see gmic_plugin_call.cpp): GMIC.ofx
# must not link libcurl (macOS: otool -L, Windows: objdump -p, or llvm-objdump -p), and the bundle
# must not contain a libcurl library (macOS Contents/Libraries, Windows Contents/Win64 or
# Contents/Win-arm64, after osxDeploy.sh or bundle_libs.sh).
# Usage: check_no_libcurl.sh <GMIC.ofx.bundle>
set -euo pipefail
bundle=${1:?usage: $0 <GMIC.ofx.bundle>}
# the plug-in binaries: macOS Contents/MacOS; Windows Contents/Win64 (x64), Contents/Win-arm64
# (ARM64), or both
case $(uname -s) in
    Darwin) dirs=MacOS ;;
    *) dirs="Win64 Win-arm64"; objdump=${OBJDUMP:-$(command -v objdump || command -v llvm-objdump || echo objdump)} ;;
esac
status=0
found=0
ok=""
for d in $dirs; do
    ofx="$bundle/Contents/$d/GMIC.ofx"
    [ -f "$ofx" ] || continue
    found=1
    case $d in
        MacOS) libs=$(otool -L "$ofx" | tail -n +2 | awk '{print $1}') ;;
        *) libs=$("$objdump" -p "$ofx" | sed -n 's/.*DLL Name: //p') ;;
    esac
    if echo "$libs" | grep -i curl; then
        echo "Error: $ofx links libcurl (see above)"; status=1
    else
        ok="$ok
OK: $ofx doesn't use libcurl; it links: $(echo $libs)"
    fi
done
[ $found = 1 ] || { echo "Error: no GMIC.ofx in $bundle/Contents/MacOS, Win64 or Win-arm64"; exit 1; }
if find "$bundle/Contents" -iname '*curl*' | grep .; then
    echo "Error: $bundle contains libcurl (see above)"; status=1
fi
[ $status = 0 ] && printf '%s\n' "${ok#?}"
exit $status
