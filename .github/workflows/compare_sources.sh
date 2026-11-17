#!/bin/bash
# Compare the G'MIC sources made by the Makefile (make sources: extracted, patched, the standard
# library and pocketfft) and its gmic_hotfixes.h with those of a CMake build directory, so that
# the two builds can't drift apart (same patches, same hot fixes). Run from the top-level
# directory, after a Makefile build with the same LICENSE and FFT as the CMake build.
# Usage: compare_sources.sh <CMake build dir>
set -euo pipefail
B=$1
status=0
files=$(sed -n 's/^GMICSOURCES *= *//p' Makefile)
for f in $files gmic_stdlib_community.h pocketfft_hdronly.h; do
    if [ "$f" = pocketfft_hdronly.h ] && [ ! -f "$B/gmic/$f" ] && [ ! -f "$f" ]; then
        continue
    fi
    if ! cmp "$f" "$B/gmic/$f"; then
        echo "FAIL: $f differs between the Makefile and CMake builds"
        status=1
    fi
done
# gmic_hotfixes.h: the same bytes (the first line, a comment, names the generator)
if ! cmp <(tail -n +2 gmic_hotfixes.h | tr -d ' \n') <(tail -n +2 "$B/generated/gmic_hotfixes.h" | tr -d ' \n'); then
    echo "FAIL: gmic_hotfixes.h differs between the Makefile and CMake builds"
    status=1
fi
[ $status = 0 ] && echo "OK: the G'MIC sources and hot fixes match"
exit $status
