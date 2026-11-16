#!/bin/bash
# Make an OFX plug-in bundle self-contained: copy the non-system shared libraries it depends on
# (recursively) into the bundle, so that it loads on a machine without the build dependencies.
# Usage: bundle_libs.sh <path to GMIC.ofx.bundle>   (the bundle is modified in place)
#
# macOS: release builds already run openfx's Examples/include/osxDeploy.sh at link time, which
#   copies the MacPorts dylibs into Contents/Libraries and rewrites their install names to
#   @rpath/<name> (the plug-in is linked with the rpath @loader_path/../Libraries), as Natron's
#   tools/jenkins/build-OSX-installer.sh does. This script completes it: it copies any library from
#   $PREFIX (default /opt/local) still referenced by the plug-in or by a bundled library, the same
#   way, removes the rpaths into $PREFIX, signs the bundle ad hoc (arm64 requires valid signatures;
#   the dylibs first, then the bundle, which seals them) and fails if anything still refers to
#   $PREFIX.
# Windows (MSYS2): the DLLs from $MINGW_PREFIX (e.g. /ucrt64) found by ntldd -R are copied next to
#   the plug-in in Contents/Win64, and a manifest listing them is embedded into the plug-in with
#   mt.exe from the Windows SDK, as in Natron's tools/jenkins/build-Windows-installer.sh: hosts load
#   the plug-in with LoadLibrary(path), which does not search the plug-in's directory for its
#   dependencies; the manifest makes the loader look for them there.
# Linux: nothing to do (see ci.yml).
# Written for macOS's /bin/bash 3.2 (no associative arrays).
set -euo pipefail

BUNDLE=${1:?usage: $0 <path to GMIC.ofx.bundle>}
BUNDLE=$(cd "$BUNDLE" && pwd)
NAME=$(basename "$BUNDLE" .ofx.bundle)

case "$(uname -s)" in
Darwin)
    PREFIX=${PREFIX:-/opt/local}
    BIN="$BUNDLE/Contents/MacOS/$NAME.ofx"
    LIBDIR="$BUNDLE/Contents/Libraries"
    RPATH="@loader_path/../Libraries"
    [ -f "$BIN" ] || { echo "Error: $BIN not found"; exit 1; }
    mkdir -p "$LIBDIR"
    shopt -s nullglob

    # dependencies of a Mach-O file that live in $PREFIX (the first line of otool -L is the file
    # itself; for a dylib, its own install name, given by otool -D, is skipped)
    prefix_deps() {
        local id
        id=$(otool -D "$1" | tail -n +2)
        otool -L "$1" | tail -n +2 | awk '{print $1}' | grep -E "^$PREFIX/" | grep -vxF "${id:-//}" || true
    }
    rpaths() {
        otool -l "$1" | awk '/cmd LC_RPATH/{r=1} r && $1=="path"{print $2; r=0}'
    }
    # install_name_tool warns that it invalidates the signature: everything is re-signed below
    int() {
        install_name_tool "$@" 2>&1 | { grep -v 'invalidate the code signature' >&2 || true; }
    }

    # copy the dependencies until no new library is found. The copies keep their basename, which
    # must be unique.
    known=$(mktemp)   # lines "basename original-path"
    todo=( "$BIN" )
    for lib in "$LIBDIR"/*.dylib; do
        echo "$(basename "$lib") $lib" >> "$known"
        todo+=( "$lib" )
    done
    while [ ${#todo[@]} -gt 0 ]; do
        f=${todo[0]}; todo=( "${todo[@]:1}" )
        for dep in $(prefix_deps "$f"); do
            base=$(basename "$dep")
            if awk -v b="$base" '$1 == b {found=1} END {exit !found}' "$known"; then
                continue
            fi
            echo "$base $dep" >> "$known"
            echo "* $base ($(realpath "$dep"))"
            cp "$(realpath "$dep")" "$LIBDIR/$base"
            chmod u+w "$LIBDIR/$base"
            int -id "@rpath/$base" "$LIBDIR/$base"
            todo+=( "$LIBDIR/$base" )
        done
    done
    rm -f "$known"

    # rewrite the references into $PREFIX to @rpath/<name>, and remove the rpaths into $PREFIX
    for f in "$BIN" "$LIBDIR"/*.dylib; do
        changes=()
        for dep in $(prefix_deps "$f"); do
            changes+=( -change "$dep" "@rpath/$(basename "$dep")" )
        done
        if [ ${#changes[@]} -gt 0 ]; then
            int "${changes[@]}" "$f"
        fi
        for rp in $(rpaths "$f"); do
            case "$rp" in "$PREFIX"/*) int -delete_rpath "$rp" "$f" ;; esac
        done
    done
    # @rpath in the bundled libraries is resolved with the rpaths of the plug-in, which loads them
    if ! rpaths "$BIN" | grep -qxF "$RPATH"; then
        int -add_rpath "$RPATH" "$BIN"
    fi

    # sign ad hoc: the libraries first, then the bundle (its main executable, and a seal of the
    # other files, the libraries included)
    for lib in "$LIBDIR"/*.dylib; do
        codesign -f -s - "$lib"
    done
    codesign -f -s - "$BUNDLE"
    codesign --verify --strict --verbose=1 "$BUNDLE"

    # check: no reference to $PREFIX is left
    status=0
    for f in "$BIN" "$LIBDIR"/*.dylib; do
        if otool -L "$f" | tail -n +2 | grep -F "$PREFIX/"; then
            echo "Error: $f still references $PREFIX"; status=1
        fi
        if rpaths "$f" | grep -F "$PREFIX/"; then
            echo "Error: $f still has an rpath into $PREFIX"; status=1
        fi
    done
    [ $status = 0 ] || exit 1
    ;;

MINGW*|MSYS*)
    PREFIX=${MINGW_PREFIX:-/ucrt64}
    BINDIR=$(find "$BUNDLE/Contents" -maxdepth 1 -type d -name 'Win*' | head -1)
    BIN="$BINDIR/$NAME.ofx"
    [ -f "$BIN" ] || { echo "Error: $NAME.ofx not found in $BUNDLE/Contents/Win*"; exit 1; }
    command -v ntldd >/dev/null || { echo "Error: ntldd not found (pacman -S ${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}-ntldd)"; exit 1; }
    WINPREFIX=$(cygpath -m "$PREFIX")   # e.g. D:/a/_temp/msys64/ucrt64
    DEPS=$(ntldd -R "$(cygpath -w "$BIN")" | tr -d '\r')
    echo "$DEPS"

    # ntldd -R lists the dependencies recursively, as "name.dll => C:\path\name.dll (0x...)";
    # keep those in $PREFIX (the system DLLs are in C:\Windows)
    ndll=0
    for p in $(echo "$DEPS" | awk '$2 == "=>" && $3 != "not" {print $3}'); do
        m=$(cygpath -m "$p")
        case "$(echo "$m" | tr '[:upper:]' '[:lower:]')" in
        "$(echo "$WINPREFIX" | tr '[:upper:]' '[:lower:]')"/*)
            u=$(cygpath -u "$p")
            if [ ! -f "$BINDIR/$(basename "$u")" ]; then
                echo "* $(basename "$u") ($u)"
                cp "$u" "$BINDIR/"
                ndll=$((ndll + 1))
            fi
            ;;
        esac
    done
    if [ $ndll = 0 ]; then
        echo "Error: ntldd found no DLL from $PREFIX: check its output above"
        exit 1
    fi
    # a DLL that ntldd could not find won't be found at load time either (the API sets, api-ms-*
    # and ext-ms-*, are resolved by the loader itself)
    if echo "$DEPS" | grep -i 'not found' | grep -viE '^[[:space:]]*(api|ext)-ms-'; then
        echo "Warning: ntldd could not find the DLLs above"
    fi

    # embed a manifest that lists the DLLs (resource 2: ISOLATIONAWARE_MANIFEST_RESOURCE_ID)
    MT=$(command -v mt.exe || true)
    if [ -z "$MT" ]; then
        MT=$(find "/c/Program Files (x86)/Windows Kits/10/bin" -name mt.exe 2>/dev/null | grep -i '/x64/' | sort -V | tail -1 || true)
    fi
    [ -n "$MT" ] || { echo "Error: mt.exe (Windows SDK) not found"; exit 1; }
    TMPD=$(mktemp -d)
    MANIFEST="$TMPD/$NAME.ofx.manifest"
    {
        echo '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        echo '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">'
        echo "<assemblyIdentity name=\"$NAME.ofx\" version=\"1.0.0.0\" type=\"win32\" processorArchitecture=\"amd64\"/>"
        for dll in "$BINDIR"/*.dll; do
            echo "<file name=\"$(basename "$dll")\"></file>"
        done
        echo '</assembly>'
    } > "$MANIFEST"
    cat "$MANIFEST"
    # mt.exe parses "-outputresource:file;2" itself: give it Windows paths, and keep MSYS2 from
    # converting the arguments
    MSYS2_ARG_CONV_EXCL='*' "$MT" -nologo -manifest "$(cygpath -w "$MANIFEST")" \
        "-outputresource:$(cygpath -w "$BIN");2"
    rm -rf "$TMPD"
    ;;

*)
    echo "$0: nothing to do on $(uname -s)"
    ;;
esac
echo "Bundled libraries in $BUNDLE:"
find "$BUNDLE/Contents" \( -name '*.dylib' -o -name '*.dll' \) | sort
