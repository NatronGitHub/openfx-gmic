#!/bin/bash
# Build a minimal HostSupport host and run describe + describeInContext on every plug-in.
# Usage: describe_plugins.sh <dir containing GMIC.ofx.bundle> <minimum number of plug-ins>
# Uses $CXX (default c++) and $HOST_LDFLAGS (extra link flags, e.g. -L/opt/local/lib).
# If $HOST_RUN_PATH is set, the host runs with PATH=$HOST_RUN_PATH (the build still uses the
# current PATH): on Windows, with a statically linked host and a PATH without the MSYS2 bin
# directory, this checks that the plug-in finds the DLLs bundled with it.
set -euo pipefail

BUNDLE_DIR=$(cd "$1" && pwd)
MIN_PLUGINS=$2
SRC=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
CXX=${CXX:-c++}

cp -R "$SRC/openfx/HostSupport/include" "$SRC/openfx/HostSupport/src" "$WORK/"
cp -R "$SRC/openfx/include" "$WORK/ofxinclude"

# HostSupport rejects duplicate param/clip definitions with kOfxStatErrExists, which plug-ins
# usually ignore: log them, so that this test can fail on them.
perl -0pi -e 's|(// "kOfxStatErrExists - if a parameter of that name exists already in this plugin")|fprintf(stderr, "DUPLICATE_PARAM %s\\n", name); $1|' "$WORK/src/ofxhParam.cpp"
perl -0pi -e 's|(            // clip names are unique)|            fprintf(stderr, "DUPLICATE_CLIP %s\\n", name);\n$1|' "$WORK/src/ofxhImageEffect.cpp"
# the patches use fprintf: don't rely on a transitive <cstdio>
for f in "$WORK/src/ofxhParam.cpp" "$WORK/src/ofxhImageEffect.cpp"; do
    printf '#include <cstdio>\n' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
done
if [ "$(grep -c DUPLICATE_PARAM "$WORK/src/ofxhParam.cpp")" != 1 ] || [ "$(grep -c DUPLICATE_CLIP "$WORK/src/ofxhImageEffect.cpp")" != 1 ]; then
    echo "Error: could not instrument HostSupport (openfx changed?), update $0"
    exit 1
fi

# shellcheck disable=SC2086
$CXX -std=c++11 -O1 -w -DOFX_EXTENSIONS_NUKE -DOFX_EXTENSIONS_NATRON \
    -I"$WORK/include" -I"$WORK/ofxinclude" \
    "$WORK"/src/*.cpp "$SRC/.github/workflows/describe_plugins.cpp" \
    ${HOST_LDFLAGS:-} -lexpat ${HOST_LIBS:-} -o "$WORK/describe_plugins"

# run with an empty HOME, so that no user G'MIC configuration (~/.config/gmic) is picked up
mkdir -p "$WORK/home"
cd "$WORK"
status=0
HOME="$WORK/home" OFX_PLUGIN_PATH="$BUNDLE_DIR" PATH="${HOST_RUN_PATH:-$PATH}" \
    ./describe_plugins "$MIN_PLUGINS" 2> describe.log || status=$?
if grep -E "DUPLICATE_PARAM|DUPLICATE_CLIP" describe.log; then
    echo "FAIL: duplicate param/clip definitions (see above)"
    status=1
fi
# the curated list of filters rendered with their preview command when the main one fails
# (Helpers/gmic_parser.cpp) names a command that is no longer a filter's main command
if grep -E "PREVIEW_FALLBACK_UNKNOWN" describe.log; then
    echo "FAIL: update the curated list in markPersistentFilters() (see above)"
    status=1
fi
# the plug-in skips a hot fix whose built-in G'MIC definition changed (see gmic_hotfixes.gmic)
if grep "openfx-gmic: Warning: hot fix" describe.log; then
    echo "FAIL: hot fixes not applied, review them in gmic_hotfixes.gmic (see above)"
    status=1
fi
if [ $status != 0 ]; then
    echo "--- last lines of the host log:"
    tail -n 30 describe.log
fi
exit $status
