#!/bin/bash
# Build a minimal HostSupport host and run describe + describeInContext on every plug-in.
# Usage: describe_plugins.sh <dir containing GMIC.ofx.bundle> <number of plug-ins>
# The number of plug-ins from the built-in standard library must be exactly that one (ci.yml's
# GMIC_PLUGIN_COUNT): a filter that a G'MIC update shows or hides (e.g. one that the parser's
# generic rules no longer hide) then fails the test, and is checked before the count is updated.
# Uses $CXX (default c++) and $HOST_LDFLAGS (extra link flags, e.g. -L/opt/local/lib).
# PLUGIN_LIST=<file>: also write the plug-ins' ids, contexts and input clips there (for render_plugins.py),
# and the plug-ins that run their preview command first. PREVIEW_FIRST_COUNT=<n>: fail unless n
# plug-ins run their preview command first. PUSH_BUTTON_COUNT=<n>: fail unless the plug-ins have n
# push buttons in all.
# The bundle's list of the plug-ins (gmic_ofx_plugins.txt), if any, must be the described plug-ins;
# DESCRIBE_USER_LIST=1 (once per CI job: it describes them all again) requires the list, and also
# checks that a user's list that disables two plug-ins leaves them out, also with a log file and in
# a profile folder with non-ASCII characters.
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
trap 'cd / && rm -rf "$WORK"' EXIT
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
    $CXX -std=c++17 -O1 -w -DOFX_EXTENSIONS_NUKE -DOFX_EXTENSIONS_NATRON \
        -I"$WORK/include" -I"$WORK/ofxinclude" \
        "$WORK"/src/*.cpp "$SRC/.github/workflows/describe_plugins.cpp" \
        ${HOST_LDFLAGS:-} -lexpat ${HOST_LIBS:-} -o "$WORK/describe_plugins"
fi

native_path() { if command -v cygpath > /dev/null; then cygpath -m "$1"; else echo "$1"; fi; }
# run_host <home> <expected number of plug-ins> <name> [<VAR>=<value>...]: runs the host with
# <home> as HOME, APPDATA and USERPROFILE (G'MIC's folder: ~/.config/gmic, %APPDATA%/gmic; the
# user source: ~/.gmic, %USERPROFILE%/user.gmic), without XDG_CONFIG_HOME and GMIC_PATH (which
# would move G'MIC's folder) and OFX_GMIC_UPDATE_FILE, unless given in <VAR>=<value>; stdout goes
# to <name>.out, stderr (the PLUGIN lines and the plug-in's log) to <name>.log. Returns the host's
# exit status.
run_host() {
    local home=$1 expected=$2 name=$3
    shift 3
    env -u XDG_CONFIG_HOME -u GMIC_PATH -u OFX_GMIC_UPDATE_FILE \
        HOME="$home" APPDATA="$(native_path "$home")" USERPROFILE="$(native_path "$home")" \
        OFX_PLUGIN_PATH="$BUNDLE_DIR" PATH="${HOST_RUN_PATH:-$PATH}" ${1+"$@"} \
        ./describe_plugins "$expected" > "$name.out" 2> "$name.log"
}
# all_described <name> <n>: did the run <name> describe exactly <n> plug-ins, with no failure?
# (through a file: with pipefail, "tr | grep -q" can fail when grep stops at a match, see below)
all_described() {
    tr -d '\r' < "$1.out" > "$1.out.txt"
    grep -qx "plugins=$2 described=$2 failed=0" "$1.out.txt"
}
# the last line of the output of the run <name>
last_line() { tr -d '\r' < "$1.out" | tail -n 1; }
cd "$WORK"
status=0
if [ -n "${DESCRIBE_MSVC_EXPAT_LIB:-}" ]; then BUNDLE_DIR=$(cygpath -m "$BUNDLE_DIR"); fi
# with an empty home, so that no user G'MIC configuration or user source is picked up, and only the
# built-in stdlib gives the plug-ins, whose number must be exact
mkdir -p "$WORK/home"
run_host "$WORK/home" "$MIN_PLUGINS" describe || status=$?
cat describe.out
# $DESCRIBE_LOG (an absolute path): keep a copy of the log (PLUGIN <id> lines, ...)
if [ -n "${DESCRIBE_LOG:-}" ]; then cp describe.log "$DESCRIBE_LOG"; fi
described=$(tr -d '\r' < describe.log | grep -c '^PLUGIN ' || true)
if [ "$status" = 0 ] && [ "$described" != "$MIN_PLUGINS" ]; then
    echo "FAIL: $described plug-ins, expected exactly $MIN_PLUGINS: check the filters that appeared or disappeared, then update GMIC_PLUGIN_COUNT in ci.yml"
    status=1
fi
if grep -E "DUPLICATE_PARAM|DUPLICATE_CLIP" describe.log; then
    echo "FAIL: duplicate param/clip definitions (see above)"
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
    run_host "$WORK/home2" "$expected" describe2 || true
    tr -d '\r' < describe2.out
    if ! all_described describe2 "$expected" ||
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
    # The same with the user's log file (README, "Messages"): the line goes to it, not to stderr
    mkdir -p "$WORK/home3/.config/gmic" "$WORK/home3/gmic"
    cp "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt" "$WORK/home3/.config/gmic/"
    cp "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt" "$WORK/home3/gmic/"
    : > "$WORK/home3/.config/gmic/gmic_ofx_log"
    : > "$WORK/home3/gmic/gmic_ofx_log"
    run_host "$WORK/home3" "$expected" describe3 || true
    cat "$WORK/home3/.config/gmic/gmic_ofx_log" "$WORK/home3/gmic/gmic_ofx_log" | tr -d '\r' > describe3.gmiclog
    summary="openfx-gmic: .*gmic_ofx_plugins.txt: 2 plug-ins disabled; unknown identifiers: eu.gmic.NoSuchPlugin"
    if ! all_described describe3 "$expected"; then
        echo "FAIL: with a log file: expected the $expected plug-ins of the user's list"
        status=1
    fi
    if ! grep -x "$summary" describe3.gmiclog > /dev/null; then
        cat describe3.gmiclog
        echo "FAIL: with a log file: expected the line that reports the user's list in gmic_ofx_log"
        status=1
    fi
    if tr -d '\r' < describe3.log | grep -x "$summary" > /dev/null; then
        echo "FAIL: with a log file: the line that reports the user's list is still on stderr"
        status=1
    fi
    # The same in a profile folder with non-ASCII characters, some of them outside Windows' ANSI
    # code page (issue #201): the user's list and the log file are still found
    home4="$WORK/home4-Frédéric-日本"
    mkdir -p "$home4/.config/gmic" "$home4/gmic"
    cp "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt" "$home4/.config/gmic/"
    cp "$WORK/home2/.config/gmic/gmic_ofx_plugins.txt" "$home4/gmic/"
    : > "$home4/.config/gmic/gmic_ofx_log"
    : > "$home4/gmic/gmic_ofx_log"
    run_host "$home4" "$expected" describe4 || true
    cat "$home4/.config/gmic/gmic_ofx_log" "$home4/gmic/gmic_ofx_log" | tr -d '\r' > describe4.gmiclog
    if ! all_described describe4 "$expected"; then
        echo "FAIL: non-ASCII profile folder: expected the $expected plug-ins of the user's list, got: $(last_line describe4)"
        status=1
    fi
    if ! grep -x "$summary" describe4.gmiclog > /dev/null; then
        cat describe4.gmiclog
        echo "FAIL: non-ASCII profile folder: expected the line that reports the user's list in gmic_ofx_log"
        status=1
    fi
fi
if [ "${DESCRIBE_UPDATE_FILE:-}" = 1 ]; then
    update_name="update$(sh "$SRC/.github/workflows/gmic_version.sh" "$SRC/Makefile" | tr -d .).gmic"
    # update_case <name> <OFX_GMIC_UPDATE_FILE> <expected number of plug-ins> [<text the log must
    #   have>] < <update file contents>
    # (in G'MIC's folder, see run_host)
    update_case() {
        local home="$WORK/home-update-$1"
        mkdir -p "$home/.config/gmic" "$home/gmic"
        cat > "$home/.config/gmic/$update_name"
        cp "$home/.config/gmic/$update_name" "$home/gmic/"
        run_host "$home" "$3" "update-$1" OFX_GMIC_UPDATE_FILE="$2" || true
        if ! all_described "update-$1" "$3"; then
            echo "FAIL: update file, $1: expected $3 plug-ins, got: $(last_line "update-$1")"
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
    update_case empty 1 "$described" < /dev/null
    printf 'fx_update_file_test :\n  f 128\n' | update_case no-gui 1 "$described"
    printf '%s' "$one_filter" | update_case opt-in-off 0 "$described" "OFX_GMIC_UPDATE_FILE"
    # a serialized CImg (still compressed): ignored, with a warning
    printf '1 uint8 little_endian\n%s' "$one_filter" | update_case compressed 1 "$described" "is compressed"
    printf '%s' "$one_filter" | update_case used 1 1
    # in a profile folder with non-ASCII characters (issue #201)
    printf '%s' "$one_filter" | update_case used-Frédéric-日本 1 1
    # the page that G'MIC's update_deprecate inserts into older update files is no plug-in
    printf '%s#@gui ________\n#@gui !<b><i>[ New Version Available! ]</i></b> : _none_, _none_\n' "$one_filter" |
        update_case new-version-page 1 1
    # star_tone is hot-fixed (gmic_hotfixes.gmic): the file's own definition is kept, with a note
    # (not a hot fix warning)
    printf '%sstar_tone :\n  f 64\n' "$one_filter" | update_case hot-fixed-command 1 1 "'star_tone' is defined by "
fi
# DESCRIBE_UPDATE_FILE=1 also checks the user source (README, "Filters"): ~/.gmic (Windows:
# %USERPROFILE%/user.gmic), whose filters come after the built-in ones, with G'MIC-Qt's rules for
# filter files (#206): nested folders are nested menu groups, "#@gui_en hide(path)" removes a
# filter or a folder (here a built-in one), blanks may come before "#@gui", a "!" before a filter
# name is not part of it, and the preview factor may be a float
if [ "${DESCRIBE_UPDATE_FILE:-}" = 1 ]; then
    home="$WORK/home-user-source"
    mkdir -p "$home"
    cat > "$home/.gmic" <<'EOF2'
#@gui <b>User Source Test</b>
#@gui <i>Nested</i>
#@gui !User Source Filter : fx_user_source_test, fx_user_source_test(1.5)+
#@gui : Value = float(0.5,0,1)
  #@gui User Source Indented : fx_user_source_test, fx_user_source_test(0)
  #@gui : Size = int(3,1,9)
#@gui_en hide(Colors/Sepia)
fx_user_source_test :
  f 128
EOF2
    cp "$home/.gmic" "$home/user.gmic"
    expected=$((described + 1)) # 2 filters more, Sepia hidden
    run_host "$home" "$expected" user-source || true
    tr -d '\r' < user-source.log > user-source.txt
    tab=$(printf '\t')
    if ! all_described user-source "$expected"; then
        echo "FAIL: user source: expected $expected plug-ins, got: $(last_line user-source)"
        status=1
    elif grep -qx "PLUGIN eu.gmic.Sepia" user-source.txt; then
        echo "FAIL: user source: hide(Colors/Sepia) did not hide Sepia"
        status=1
    elif ! grep -qx "GROUPING eu.gmic.UserSourceFilter${tab}GMIC/User Source Test/Nested${tab}GMIC User Source Filter" user-source.txt ||
        ! grep -qx "GROUPING eu.gmic.UserSourceIndented${tab}GMIC/User Source Test/Nested${tab}GMIC User Source Indented" user-source.txt; then
        grep '^GROUPING eu.gmic.UserSource' user-source.txt || true
        echo "FAIL: user source: expected its two filters in the nested group GMIC/User Source Test/Nested"
        status=1
    else
        echo "user source: $expected plug-ins, as expected"
    fi
fi
# the plug-ins that run their preview command before their main command (#206): the plug-in logs
# "openfx-gmic: previewFirst <id>" for each when it loads (gmic_plugin.cpp,
# loadPluginGlobalData(): keep the text the same). Listed, and checked against
# $PREVIEW_FIRST_COUNT if set (ci.yml: a G'MIC bump or a change of the rule in
# Helpers/gmic_parser.cpp changes it). Each id once: the host loads the plug-in twice (HostSupport's
# scan unloads the binary after OfxGetNumberOfPlugins, and the describe loads it again), and each
# load logs the lines again.
tr -d '\r' < describe.log | sed -n -e 's/^openfx-gmic: previewFirst //p' | LC_ALL=C sort -u > preview_first.txt
preview_first=$(($(wc -l < preview_first.txt)))
echo "$preview_first plug-ins run their preview command first:" $(cat preview_first.txt)
if [ -n "${PREVIEW_FIRST_COUNT:-}" ] && [ "$preview_first" != "$PREVIEW_FIRST_COUNT" ]; then
    echo "FAIL: $preview_first plug-ins run their preview command first, expected PREVIEW_FIRST_COUNT=$PREVIEW_FIRST_COUNT (update it in ci.yml if the change is right)"
    status=1
fi
# the push buttons (#174: G'MIC's button() parameters, which were checkboxes): listed by
# describe_plugins (one "PUSHBUTTON <id><tab><parameter>" line each), counted, and checked against
# $PUSH_BUTTON_COUNT if set (ci.yml: a G'MIC bump changes it)
tr -d '\r' < describe.log | sed -n -e 's/^PUSHBUTTON //p' > push_buttons.txt
push_buttons=$(($(wc -l < push_buttons.txt)))
echo "$push_buttons push buttons, in $(($(cut -f 1 push_buttons.txt | LC_ALL=C sort -u | wc -l))) plug-ins"
if [ -n "${PUSH_BUTTON_COUNT:-}" ] && [ "$push_buttons" != "$PUSH_BUTTON_COUNT" ]; then
    cat push_buttons.txt
    echo "FAIL: $push_buttons push buttons, expected PUSH_BUTTON_COUNT=$PUSH_BUTTON_COUNT (update it in ci.yml if the change is right)"
    status=1
fi
# PLUGIN_LIST=<file>: the plug-ins, their context and input clips, for render_plugins.py, then a
# "#previewFirst<tab><id>" line for each plug-in that runs its preview command first
if [ -n "${PLUGIN_LIST:-}" ]; then
    sed -n -e 's/^RENDERINFO //p' describe.log > "$PLUGIN_LIST"
    awk '{ printf "#previewFirst\t%s\n", $0 }' preview_first.txt >> "$PLUGIN_LIST"
fi
if [ $status != 0 ]; then
    echo "--- last lines of the host log:"
    tail -n 30 describe.log
fi
exit $status
