#!/bin/sh
# Print the G'MIC version that gmic_sources.txt pins (GMICVER, e.g. 4.0.8), which CMakeLists.txt
# also reads. Usage: gmic_version.sh [<gmic_sources.txt>] (default: gmic_sources.txt)
set -e
ver=$(sed -n 's/^GMICVER *= *//p' "${1:-gmic_sources.txt}" | tr -d '\r ')
[ -n "$ver" ] || { echo "Error: no GMICVER in ${1:-gmic_sources.txt}" >&2; exit 1; }
echo "$ver"
