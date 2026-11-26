#!/bin/bash
# Make a checkout of openfx-gmic build G'MIC's development sources instead of its pinned release
# (#226): GreycLab/gmic (develop), GreycLab/CImg (develop) and GreycLab/gmic-community (master),
# checked out at the commits to test.
# Usage: gmic_develop_sources.sh <gmic> <CImg> <gmic-community> <openfx-gmic checkout> <report.md>
#
# A release tarball's gmic_stdlib_community.h is G'MIC's filter update file (update<version>.gmic:
# the standard library and the community filters, as G'MIC's 'upload_filters' command makes it
# for gmic.eu), serialized (G'MIC's Makefile, target do_gmic_stdlib_community.h). The develop
# sources have no such header, so it is made the same way: a G'MIC interpreter is built from the
# develop sources, with their bare src/gmic_stdlib.gmic as its standard library, and runs
# 'upload_filters' on the checkouts given here, without its 'git pull' and uploads; then
# gmic_stdlib_header.py serializes the result.
#
# The checkout then gets a tarball as the release ones (gmic_<version>.tar.gz, <version> from
# gmic.h's gmic_version, e.g. 4.1.0 for 410, with the files that the build extracts and
# gmic_cli.cpp, for fft_smoke.sh), and its Makefile, which CMakeLists.txt and the tests read,
# names that version, with the tarball's checksum: as a G'MIC bump would, in this checkout only.
# Each local patch (CIMG_PATCHES, GMIC_PATCHES) is tried on the develop sources, in order: one
# that fails is reported and left out of the Makefile's lists, not an error. The report (Markdown)
# gives the commits, the version and each patch's outcome.
# Uses $CXX (default c++), and pkg-config for zlib.
set -euo pipefail

GMIC=$(cd "$1" && pwd)
CIMG=$(cd "$2" && pwd)
COMMUNITY=$(cd "$3" && pwd)
TREE=$(cd "$4" && pwd)
REPORT=$5
HERE=$(cd "$(dirname "$0")" && pwd)
CXX=${CXX:-c++}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

commit() { git -C "$1" rev-parse HEAD; }
number=$(sed -n -e 's/^#define gmic_version \([0-9][0-9][0-9]\)$/\1/p' "$GMIC/src/gmic.h")
[ -n "$number" ] || { echo "Error: no '#define gmic_version <3 digits>' in $GMIC/src/gmic.h"; exit 1; }
ver=${number:0:1}.${number:1:1}.${number:2:1}
echo "G'MIC develop: version $ver"

# The interpreter, with the develop standard library (as is, not yet an update file)
mkdir "$WORK/cli"
cp "$GMIC/src/gmic.cpp" "$GMIC/src/gmic.h" "$GMIC/src/gmic_cli.cpp" "$CIMG/CImg.h" "$WORK/cli/"
python3 "$HERE/gmic_stdlib_header.py" "$GMIC/src/gmic_stdlib.gmic" "$WORK/cli/gmic_stdlib_community.h" \
    --note "bootstrap: G'MIC's src/gmic_stdlib.gmic at $(commit "$GMIC")"
echo "Building a G'MIC interpreter from the develop sources"
# shellcheck disable=SC2046
(cd "$WORK/cli" &&
    $CXX -O1 -w -Dgmic_core -Dcimg_display=0 -Dcimg_use_zlib $(pkg-config --cflags zlib) -c gmic.cpp -o gmic.o &&
    $CXX -O1 -w -Dcimg_display=0 $(pkg-config --cflags zlib) -c gmic_cli.cpp -o gmic_cli.o &&
    $CXX gmic.o gmic_cli.o -o gmic $(pkg-config --libs zlib) -lpthread)

# upload_filters reads $HOME/work/src/gmic and $HOME/work/src/gmic-community, and writes
# ${_path_rc}update<version>.gmic ($HOME/.config/gmic/ on Linux and macOS)
home="$WORK/home"
mkdir -p "$home/work/src" "$home/.config/gmic"
ln -s "$GMIC" "$home/work/src/gmic"
ln -s "$COMMUNITY" "$home/work/src/gmic-community"
# its definition, from the line 'upload_filters :' to the next command, without its 'git pull's;
# _upload (to the gmic.eu server) does nothing
# shellcheck disable=SC2016
awk '/^upload_filters :/ { p = 1; print; next } p && /^[^ \t#]/ { exit } p' "$GMIC/src/gmic_stdlib.gmic" |
    grep -v '^ *x "cd "\$HOME"/work/src/[a-z-]* && git pull"$' > "$WORK/upload_filters.gmic"
grep -q '^upload_filters :' "$WORK/upload_filters.gmic" || { echo "Error: no upload_filters command in gmic_stdlib.gmic"; exit 1; }
if grep -n 'git pull' "$WORK/upload_filters.gmic"; then
    echo "Error: upload_filters still runs 'git pull' (G'MIC changed it): update $0"
    exit 1
fi
# shellcheck disable=SC2016
printf '_upload : skip "${2=""}","${3=0}"\n' >> "$WORK/upload_filters.gmic"
echo "Making the update file (upload_filters)"
(cd "$WORK" && env -u XDG_CONFIG_HOME -u GMIC_PATH HOME="$home" ./cli/gmic -v 0 m upload_filters.gmic upload_filters)
update="$home/.config/gmic/update$number.gmic"
if ! grep -q '^#@gui ' "$update"; then
    echo "Error: upload_filters wrote no filters into $update"
    exit 1
fi

src="$WORK/gmic-$ver/src"
mkdir -p "$src"
cp "$GMIC/src/gmic.cpp" "$GMIC/src/gmic.h" "$GMIC/src/gmic_cli.cpp" "$CIMG/CImg.h" "$COMMUNITY/libcgmic/gmic_libc.h" "$src/"
python3 "$HERE/gmic_stdlib_header.py" "$update" "$src/gmic_stdlib_community.h" \
    --note "openfx-gmic: G'MIC develop $ver, update$number.gmic made by upload_filters from" \
    --note "GreycLab/gmic $(commit "$GMIC") and GreycLab/gmic-community $(commit "$COMMUNITY")"
cp "$update" "$TREE/update$number.gmic"
tar -C "$WORK" -czf "$TREE/gmic_$ver.tar.gz" "gmic-$ver"
if command -v sha256sum > /dev/null; then sum=$(sha256sum "$TREE/gmic_$ver.tar.gz" | cut -d' ' -f1)
else sum=$(shasum -a 256 "$TREE/gmic_$ver.tar.gz" | cut -d' ' -f1); fi

# The local patches, in the Makefile's order, each on the result of the previous ones
cimg_kept="" gmic_kept=""
cp "$src/CImg.h" "$src/gmic.cpp" "$WORK/"
{
    echo "### G'MIC sources"
    echo
    echo "| Repository | Commit |"
    echo "|---|---|"
    row() { echo "| $1 | [\`$(commit "$2")\`](https://github.com/$1/commit/$(commit "$2")) |"; }
    row GreycLab/gmic "$GMIC"
    row GreycLab/CImg "$CIMG"
    row GreycLab/gmic-community "$COMMUNITY"
    echo
    echo "Version $ver (gmic_version $number); standard library: $(grep -c '^#@gui [^:_][^:]* :' "$update") filters, from upload_filters."
    echo
    echo "### Local patches"
    echo
    echo "| Patch | File | Outcome |"
    echo "|---|---|---|"
} > "$REPORT"
for list in CIMG_PATCHES:CImg.h GMIC_PATCHES:gmic.cpp; do
    var=${list%%:*} file=${list#*:}
    kept=""
    # shellcheck disable=SC2013
    for p in $(sed -n -e "s/^$var *= *//p" "$TREE/Makefile"); do
        if out=$(cd "$WORK" && sh "$TREE/$p" "$file" 2>&1); then
            kept="$kept $p"
            case $out in
                *"not patched"*|*"already"*) outcome="not needed: $(echo "$out" | tail -n 1)";;
                *) outcome="applied";;
            esac
        else
            outcome="**does not apply** (left out): $(echo "$out" | head -n 1)"
        fi
        echo "$p: $outcome"
        echo "| \`$p\` | $file | ${outcome//|/\\|} |" >> "$REPORT"
    done
    kept=${kept# }
    [ -n "$kept" ] || { echo "Error: no $var patch applies (CMakeLists.txt needs one): update $0"; exit 1; }
    case $var in CIMG_PATCHES) cimg_kept=$kept;; GMIC_PATCHES) gmic_kept=$kept;; esac
done
# the Makefile: GMICVER, its checksum (before 'GMICSRC_SHA256=', which picks the one of GMICVER)
# and the patches that apply
awk -v ver="$ver" -v sum="$sum" -v cimg="$cimg_kept" -v gmic="$gmic_kept" '
    /^GMICVER *=/ { print "GMICVER=" ver; next }
    index($0, "GMICSRC_SHA256_" ver "=") == 1 { next }
    /^GMICSRC_SHA256 *=/ { print "GMICSRC_SHA256_" ver "=" sum }
    /^CIMG_PATCHES *=/ { print "CIMG_PATCHES = " cimg; next }
    /^GMIC_PATCHES *=/ { print "GMIC_PATCHES = " gmic; next }
    { print }' "$TREE/Makefile" > "$WORK/Makefile"
cp "$WORK/Makefile" "$TREE/Makefile"
if ! grep -qx "GMICVER=$ver" "$TREE/Makefile" || ! grep -qx "GMICSRC_SHA256_$ver=$sum" "$TREE/Makefile"; then
    echo "Error: could not set GMICVER and GMICSRC_SHA256_$ver in $TREE/Makefile: update $0"
    exit 1
fi
echo "$TREE: G'MIC $ver from the develop sources (gmic_$ver.tar.gz, sha256 $sum)"
