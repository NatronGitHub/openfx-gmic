#!/bin/sh
# Print the G'MIC version that the Makefile pins (GMICVER, e.g. 4.0.8), which CMakeLists.txt
# also reads. Usage: gmic_version.sh [<Makefile>] (default: Makefile)
set -e
ver=$(sed -n 's/^GMICVER *= *//p' "${1:-Makefile}" | tr -d '\r ')
[ -n "$ver" ] || { echo "Error: no GMICVER in ${1:-Makefile}" >&2; exit 1; }
echo "$ver"
