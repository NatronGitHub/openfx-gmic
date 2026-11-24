#!/bin/bash
# Build a minimal HostSupport host and run describe + describeInContext on every plug-in.
# Usage: describe_plugins.sh <dir containing GMIC.ofx.bundle> <minimum number of plug-ins>
# Uses $CXX (default c++) and $HOST_LDFLAGS (extra link flags, e.g. -L/opt/local/lib).
# PLUGIN_LIST=<file>: also write the plug-ins' ids, contexts and input clips there (for render_plugins.py).
# The bundle's list of the plug-ins (gmic_ofx_plugins.txt), if any, must be the described plug-ins;
# DESCRIBE_USER_LIST=1 (once per CI job: it describes them all again) requires the list, and also
# checks that a user's list that disables two plug-ins leaves them out.
# DESCRIBE_UPDATE_FILE=1 (once per CI job too) also checks G'MIC's update file (update<GMICVER>.gmic,
# read with OFX_GMIC_UPDATE_FILE=1): ignored when empty, without #@gui or without the opt-in, used
# otherwise, and the hot fixes leave a command that it defines another way alone.
# If $HOST_RUN_PATH is set, the host runs with PATH=$HOST_RUN_PATH (the build still uses the
# current PATH): on Windows, with a statically linked host and a PATH without the MSYS2 bin
# directory, this checks that the plug-in finds the DLLs bundled with it.
# MSVC (Git for Windows' bash with cl.exe in PATH, e.g. after vcvars): DESCRIBE_MSVC_EXPAT_LIB=<a
# static expat .lib built with /MT> builds the host with cl.exe instead of $CXX, and
# DESCRIBE_MSVC_CFLAGS adds compile flags (e.g. -I<expat's include directory>, -arm64EC) and
# DESCRIBE_MSVC_LDFLAGS link flags (e.g. -MACHINE:ARM64EC).
set -euo pipefail

BUNDLE_DIR=$(cd "$1" && pwd)
if [ -n "${PLUGIN_LIST:-}" ]; then # absolute: the script changes directory
    PLUGIN_LIST="$(cd "$(dirname "$PLUGIN_LIST")" && pwd)/$(basename "$PLUGIN_LIST")"
fi
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

if [ -n "${DESCRIBE_MSVC_EXPAT_LIB:-}" ]; then
    # "-" options, not "/": MSYS would convert those as paths; shell32: SHGetFolderPathW
    w() { cygpath -m "$1"; }
    # absolute: cl.exe runs in $WORK
    DESCRIBE_MSVC_EXPAT_LIB="$(cd "$(dirname "$DESCRIBE_MSVC_EXPAT_LIB")" && pwd)/$(basename "$DESCRIBE_MSVC_EXPAT_LIB")"
    (cd "$WORK" && MSYS2_ARG_CONV_EXCL='*' cl -nologo -std:c++17 -O1 -EHsc -MT -w -DOFX_EXTENSIONS_NUKE -DOFX_EXTENSIONS_NATRON \
        -DXML_STATIC -I"$(w "$WORK/include")" -I"$(w "$WORK/ofxinclude")" ${DESCRIBE_MSVC_CFLAGS:-} \
        $(for f in "$WORK"/src/*.cpp; do w "$f"; done) "$(w "$SRC/.github/workflows/describe_plugins.cpp")" \
        -Fe:describe_plugins.exe -link ${DESCRIBE_MSVC_LDFLAGS:-} "$(w "$DESCRIBE_MSVC_EXPAT_LIB")" shell32.lib) > "$WORK/cl.log" 2>&1 ||
        { tail -n 50 "$WORK/cl.log"; exit 1; }
else
    # shellcheck disable=SC2086
    $CXX -std=c++11 -O1 -w -DOFX_EXTENSIONS_NUKE -DOFX_EXTENSIONS_NATRON \
        -I"$WORK/include" -I"$WORK/ofxinclude" \
        "$WORK"/src/*.cpp "$SRC/.github/workflows/describe_plugins.cpp" \
        ${HOST_LDFLAGS:-} -lexpat ${HOST_LIBS:-} -o "$WORK/describe_plugins"
fi

# run with an empty HOME (and APPDATA, on Windows), so that no user G'MIC configuration
# (~/.config/gmic, %APPDATA%/gmic) is picked up
native_path() { if command -v cygpath > /dev/null; then cygpath -m "$1"; else echo "$1"; fi; }
mkdir -p "$WORK/home"
cd "$WORK"
status=0
if [ -n "${DESCRIBE_MSVC_EXPAT_LIB:-}" ]; then BUNDLE_DIR=$(cygpath -m "$BUNDLE_DIR"); fi
HOME="$WORK/home" APPDATA="$(native_path "$WORK/home")" OFX_PLUGIN_PATH="$BUNDLE_DIR" PATH="${HOST_RUN_PATH:-$PATH}" \
    ./describe_plugins "$MIN_PLUGINS" 2> describe.log || status=$?
# $DESCRIBE_LOG (an absolute path): keep a copy of the log (PLUGIN <id> lines, ...)
if [ -n "${DESCRIBE_LOG:-}" ]; then cp describe.log "$DESCRIBE_LOG"; fi
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
# the curated list of hidden batch/interactive filters (Helpers/gmic_parser.cpp) names a command
# that is no longer a filter's main command
if grep -E "HIDDEN_FILTER_UNKNOWN" describe.log; then
    echo "FAIL: update the curated list in hideBatchFilters() (see above)"
    status=1
fi
# a parameter type the parser doesn't know (e.g. new in a G'MIC bump): pluginSetup passes 0 for it
if grep "has an unknown type" describe.log; then
    echo "FAIL: unknown G'MIC parameter types, support them in Helpers/gmic_parser.cpp and pluginSetup (see above)"
    status=1
fi
# the plug-in skips a hot fix whose built-in G'MIC definition changed (see gmic_hotfixes.gmic)
if grep "openfx-gmic: Warning: hot fix" describe.log; then
    echo "FAIL: hot fixes not applied, review them in gmic_hotfixes.gmic (see above)"
    status=1
fi
# The bundle's list of the plug-ins (README, "Choosing the plug-ins") has the described plug-ins,
# all enabled; DESCRIBE_USER_LIST=1: a user's list that disables two of them leaves them out.
SHIPPED_LIST="$BUNDLE_DIR/GMIC.ofx.bundle/Contents/Resources/gmic_ofx_plugins.txt"
if [ ! -f "$SHIPPED_LIST" ] && [ "${DESCRIBE_USER_LIST:-}" = 1 ]; then
    echo "FAIL: no list of the plug-ins in the bundle, $SHIPPED_LIST"
    status=1
elif [ ! -f "$SHIPPED_LIST" ]; then
    echo "Note: no list of the plug-ins in the bundle (a GMIC_PLUGIN_LIST=OFF build?): not checked"
else
    # (comm and sort, from coreutils: diff may not be installed, e.g. in MSYS2; LC_ALL=C: the same
    # order for both)
    sed -n -e 's/^PLUGIN //p' describe.log | tr -d '\r' | LC_ALL=C sort > described.txt
    grep -v '^[[:space:]]*#' "$SHIPPED_LIST" | sed -e 's/#.*//' | tr -d ' \t\r' | grep . | LC_ALL=C sort > listed.txt || true
    LC_ALL=C comm -3 listed.txt described.txt > list.diff
    if [ -s list.diff ] || [ ! -s listed.txt ]; then
        # (od -c: also the invisible characters)
        echo "the first differences, only in the list, or (after a tab) only described:"
        head -n 10 list.diff | od -c | head -n 20
        echo "FAIL: the bundle's list of the plug-ins ($(($(wc -l < listed.txt))) entries) is not the described plug-ins ($(($(wc -l < described.txt))))"
        status=1
    fi
fi
if [ -f "$SHIPPED_LIST" ] && [ "${DESCRIBE_USER_LIST:-}" = 1 ]; then
    first=$(head -n 1 listed.txt)
    last=$(tail -n 1 listed.txt)
    mkdir -p "$WORK/home2/.config/gmic"
    # (as a user would: commented out with "# " and with "##"; and stale lines)
    sed -e "s/^$first /# $first /" -e "s/^$last /##$last /" "$SHIPPED_LIST" > "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt"
    printf '# eu.gmic.NoSuchPlugin  # a filter that G'"'"'MIC removed\neu.gmic.NoSuchPlugin2\n' >> "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt"
    mkdir -p "$WORK/home2/gmic" # (Windows: %APPDATA%/gmic)
    cp "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt" "$WORK/home2/gmic/"
    expected=$(($(wc -l < listed.txt) - 2))
    # (no "tr ... | grep -q" below: with pipefail, tr fails with SIGPIPE when grep -q stops at a
    # match, which would hide that match)
    HOME="$WORK/home2" APPDATA="$(native_path "$WORK/home2")" OFX_PLUGIN_PATH="$BUNDLE_DIR" PATH="${HOST_RUN_PATH:-$PATH}" \
        ./describe_plugins "$expected" > describe2.out 2> describe2.log || true
    tr -d '\r' < describe2.out > describe2.txt
    cat describe2.txt
    if ! grep -qx "plugins=$expected described=$expected failed=0" describe2.txt ||
        tr -d '\r' < describe2.log | grep -xF -e "PLUGIN $first" -e "PLUGIN $last" > /dev/null; then
        echo "FAIL: a user's list that disables $first and $last: expected the $expected other plug-ins"
        status=1
    fi
    # (one stderr line: the disabled plug-ins and the disabled identifiers that are no plug-in)
    if ! tr -d '\r' < describe2.log | grep -x "openfx-gmic: .*gmic_ofx_plugins.txt: 2 plug-ins disabled; unknown identifiers: eu.gmic.NoSuchPlugin" > /dev/null; then
        tr -d '\r' < describe2.log | grep '^openfx-gmic: ' || true
        echo "FAIL: a user's list that disables $first and $last: expected the line that reports it on stderr"
        status=1
    fi
fi
if [ "${DESCRIBE_UPDATE_FILE:-}" = 1 ]; then
    builtin_count=$(tr -d '\r' < describe.log | grep -c '^PLUGIN ' || true)
    update_name="update$(sed -n -e 's/^GMICVER *= *//p' "$SRC/Makefile" | tr -d '.\r ').gmic"
    # update_case <name> <OFX_GMIC_UPDATE_FILE> <expected number of plug-ins> [<text the log must
    #   have>] < <update file contents>
    # G'MIC's folder (gmic::path_rc()): $HOME/.config/gmic, %APPDATA%/gmic on Windows; without
    # XDG_CONFIG_HOME and GMIC_PATH, which would move it
    update_case() {
        local home="$WORK/home-update-$1"
        mkdir -p "$home/.config/gmic" "$home/gmic"
        cat > "$home/.config/gmic/$update_name"
        cp "$home/.config/gmic/$update_name" "$home/gmic/"
        env -u XDG_CONFIG_HOME -u GMIC_PATH OFX_GMIC_UPDATE_FILE="$2" HOME="$home" APPDATA="$(native_path "$home")" \
            OFX_PLUGIN_PATH="$BUNDLE_DIR" PATH="${HOST_RUN_PATH:-$PATH}" \
            ./describe_plugins "$3" > "update-$1.out" 2> "update-$1.log" || true
        if ! tr -d '\r' < "update-$1.out" | grep -qx "plugins=$3 described=$3 failed=0"; then
            echo "FAIL: update file, $1: expected $3 plug-ins, got: $(tr -d '\r' < "update-$1.out" | tail -n 1)"
            status=1
        elif grep "openfx-gmic: Warning: hot fix" "update-$1.log"; then
            echo "FAIL: update file, $1: hot fix warnings (see above)"
            status=1
        elif [ -n "${4:-}" ] && ! grep -qF -- "$4" "update-$1.log"; then
            echo "FAIL: update file, $1: no \"$4\" in the plug-in's log"
            status=1
        else
            echo "update file, $1: $3 plug-ins, as expected"
        fi
    }
    one_filter='#@gui Update File Test : fx_update_file_test, fx_update_file_test
fx_update_file_test :
  f 128
'
    update_case empty 1 "$builtin_count" < /dev/null
    printf 'fx_update_file_test :\n  f 128\n' | update_case no-gui 1 "$builtin_count"
    printf '%s' "$one_filter" | update_case opt-in-off 0 "$builtin_count" "OFX_GMIC_UPDATE_FILE"
    # a serialized CImg (still compressed): ignored, with a warning
    printf '1 uint8 little_endian\n%s' "$one_filter" | update_case compressed 1 "$builtin_count" "is compressed"
    printf '%s' "$one_filter" | update_case used 1 1
    # the page that G'MIC's update_deprecate inserts into older update files is no plug-in
    printf '%s#@gui ________\n#@gui !<b><i>[ New Version Available! ]</i></b> : _none_, _none_\n' "$one_filter" |
        update_case new-version-page 1 1
    # star_tone is hot-fixed (gmic_hotfixes.gmic): the file's own definition is kept, with a note
    # (not a hot fix warning)
    printf '%sstar_tone :\n  f 64\n' "$one_filter" | update_case hot-fixed-command 1 1 "'star_tone' is defined by "
fi
# PLUGIN_LIST=<file>: the plug-ins, their context and input clips, for render_plugins.py
if [ -n "${PLUGIN_LIST:-}" ]; then
    sed -n -e 's/^RENDERINFO //p' describe.log > "$PLUGIN_LIST"
fi
if [ $status != 0 ]; then
    echo "--- last lines of the host log:"
    tail -n 30 describe.log
fi
exit $status
