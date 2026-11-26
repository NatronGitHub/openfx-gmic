#!/bin/bash
# Render Add Grain, which downloads its grain images (gmic_grain.gmz, from gmic.eu) into G'MIC's
# cache, here an empty GMIC_CACHE_PATH under $RUNNER_TEMP, and check that they were downloaded
# (gmic.eu may refuse downloads). No libcurl: G'MIC downloads with the system's curl command
# (cimg::load_network; macOS /usr/bin/curl, Windows System32's curl.exe, Linux curl from PATH; see
# gmic_plugin_call.cpp). OFX_GMIC_TEST_CURL_ONLY (gmic_plugin_call.cpp): no wget or PowerShell
# fallback, so the download succeeds only through the curl command. (Color Presets downloads only
# for a preset other than the default.)
# Usage, after a build with GMIC_BUILD_TESTS=ON and the describe test (which writes plugins.txt):
#   check_download.sh <hostDemo build dir> <plug-in dir> <plug-in list>
# Uses $PYTHON (default python3).
set -euo pipefail
host=$1 plugins=$2 list=$3
cache=$RUNNER_TEMP/gmic-cache
gmic_cache=$cache
if command -v cygpath > /dev/null; then # Windows
  cache=$(cygpath -u "$RUNNER_TEMP")/gmic-cache
  gmic_cache=$(cygpath -m "$cache")
fi
mkdir -p "$cache"
OFX_GMIC_TEST_CURL_ONLY=1 GMIC_CACHE_PATH="$gmic_cache/" "${PYTHON:-python3}" .github/workflows/render_plugins.py --host "$host" \
  --plugins "$plugins" --list "$list" --only eu.gmic.AddGrain --work render-download
find "$cache" -name 'gmic_grain*' -size +0 -exec ls -l {} + | grep . ||
  { echo "FAIL: gmic_grain.gmz was not downloaded"; exit 1; }
