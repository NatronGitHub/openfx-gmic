#!/bin/bash
# Make an OFX plug-in bundle self-contained: copy the non-system shared libraries it depends on
# (recursively) into the bundle, so that it loads on a machine without the build dependencies.
# Usage: bundle_libs.sh <path to GMIC.ofx.bundle>   (the bundle is modified in place; running it
# again on the same bundle is harmless)
#
# macOS: release builds already run openfx's Examples/include/osxDeploy.sh at link time, which
#   copies the MacPorts dylibs into Contents/Libraries and rewrites their install names to
#   @rpath/<name> (the plug-in is linked with the rpath @loader_path/../Libraries), as Natron's
#   tools/jenkins/build-OSX-installer.sh does. This script completes it: it copies any library from
#   $PREFIX (default /opt/local) still referenced by the plug-in or by a bundled library, directly
#   or through @rpath (resolved with the library's original rpaths), the same way, removes the
#   rpaths into $PREFIX, signs the bundle ad hoc (arm64 requires valid signatures; the dylibs
#   first, then the bundle, which seals them), and fails if a dependency is neither a system
#   library (/usr/lib, /System) nor bundled.
# Windows (MSYS2): the DLLs from $MINGW_PREFIX/bin (e.g. /ucrt64/bin) imported by the plug-in,
#   directly or through these DLLs (objdump -p), are copied next to the plug-in in Contents/Win64,
#   the plug-in is stripped, and a manifest listing the DLLs is embedded into it with mt.exe from
#   the Windows SDK, as in Natron's tools/jenkins/build-Windows-installer.sh: hosts load the
#   plug-in with LoadLibrary(path), which does not search the plug-in's directory for its
#   dependencies; the manifest makes the loader look for them there. An import found in
#   $MINGW_PREFIX/bin is bundled, also if System32 has a DLL with the same name; one that is in
#   neither is an error.
# Linux: nothing to do (see ci.yml).
#
# Limitations of the resulting bundles:
# - libcurl and OpenSSL keep their build-time configuration paths (CA certificates, openssl.cnf in
#   /opt/local or the MSYS2 prefix). G'MIC's downloads (CImg's cimg::load_network) disable TLS
#   certificate verification, so they don't need the CA file; a missing openssl.cnf is ignored.
# - macOS: the binaries require the macOS version of the build machine or newer (their minos),
#   and a downloaded bundle must be unquarantined: xattr -dr com.apple.quarantine GMIC.ofx.bundle
# - Windows: the bundled libstdc++/libgcc_s/libgomp/libwinpthread DLLs have the same names as the
#   ones of an MSYS2-built host (Natron): a DLL of that name already loaded by the host is used
#   instead of the bundled one.
#
# Written for macOS's /bin/bash 3.2 (no associative arrays, no inherit_errexit: the failures of
# commands in $(...) are checked explicitly).
set -euo pipefail

BUNDLE=${1:?usage: $0 <path to GMIC.ofx.bundle>}
BUNDLE=$(cd "$BUNDLE" && pwd)
NAME=$(basename "$BUNDLE" .ofx.bundle)
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT

case "$(uname -s)" in
Darwin)
    PREFIX=${PREFIX:-/opt/local}
    BIN="$BUNDLE/Contents/MacOS/$NAME.ofx"
    LIBDIR="$BUNDLE/Contents/Libraries"
    RPATH="@loader_path/../Libraries"
    [ -f "$BIN" ] || { echo "Error: $BIN not found"; exit 1; }
    mkdir -p "$LIBDIR"
    shopt -s nullglob

    # dependencies of a Mach-O file (the first line of otool -L is the file itself; for a dylib,
    # its own install name, given by otool -D, is skipped)
    deps_of() {
        local out id
        out=$(otool -L "$1") || return 1
        id=$(otool -D "$1") || return 1
        id=$(printf '%s\n' "$id" | tail -n +2)
        printf '%s\n' "$out" | tail -n +2 | awk '{print $1}' | { grep -vxF "${id:-//}" || true; }
    }
    rpaths_of() {
        local out
        out=$(otool -l "$1") || return 1
        printf '%s\n' "$out" | awk '/cmd LC_RPATH/{r=1} r && $1=="path"{print $2; r=0}'
    }
    # install_name_tool warns that it invalidates the signature: everything is re-signed below
    int() {
        local out
        out=$(install_name_tool "$@" 2>&1) || { echo "$out"; echo "Error: install_name_tool $* failed"; exit 1; }
        printf '%s\n' "$out" | { grep -v -e 'invalidate the code signature' -e '^$' >&2 || true; }
    }

    # copy the dependencies until no new library is found. The copies keep their basename, which
    # must be unique. todo_f: files to scan, todo_d: their original directory (for @loader_path in
    # their rpaths)
    known="$TMPD/known"   # basenames of the bundled libraries
    : > "$known"
    todo_f=( "$BIN" ); todo_d=( "$BUNDLE/Contents/MacOS" )
    for lib in "$LIBDIR"/*.dylib; do
        basename "$lib" >> "$known"
        todo_f+=( "$lib" ); todo_d+=( "$LIBDIR" )
    done
    while [ ${#todo_f[@]} -gt 0 ]; do
        f=${todo_f[0]}; d=${todo_d[0]}
        todo_f=( "${todo_f[@]:1}" ); todo_d=( "${todo_d[@]:1}" )
        deps=$(deps_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        rps=$(rpaths_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        for dep in $deps; do
            src=
            case "$dep" in
            "$PREFIX"/*) src=$dep ;;
            @rpath/*)
                base=${dep#@rpath/}
                grep -qxF "$base" "$known" && continue
                for rp in $rps; do
                    rp=${rp/#@loader_path/$d}
                    case "$rp" in "$PREFIX"/*) ;; *) continue ;; esac
                    if [ -f "$rp/$base" ]; then src="$rp/$base"; break; fi
                done
                ;;
            esac
            [ -n "$src" ] || continue   # system library, or unresolved: see the final check
            base=$(basename "$src")
            grep -qxF "$base" "$known" && continue
            echo "$base" >> "$known"
            real=$(realpath "$src")
            echo "* $base ($real)"
            cp "$real" "$LIBDIR/$base"
            chmod u+w "$LIBDIR/$base"
            int -id "@rpath/$base" "$LIBDIR/$base"
            todo_f+=( "$LIBDIR/$base" ); todo_d+=( "$(dirname "$src")" )
        done
    done

    # rewrite the references into $PREFIX to @rpath/<name>, and remove the rpaths into $PREFIX
    for f in "$BIN" "$LIBDIR"/*.dylib; do
        deps=$(deps_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        rps=$(rpaths_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        changes=()
        for dep in $deps; do
            case "$dep" in "$PREFIX"/*) changes+=( -change "$dep" "@rpath/$(basename "$dep")" ) ;; esac
        done
        if [ ${#changes[@]} -gt 0 ]; then
            int "${changes[@]}" "$f"
        fi
        for rp in $rps; do
            case "$rp" in "$PREFIX"/*) int -delete_rpath "$rp" "$f" ;; esac
        done
    done
    # @rpath in the bundled libraries is resolved with the rpaths of the plug-in, which loads them
    rps=$(rpaths_of "$BIN") || { echo "Error: otool failed on $BIN"; exit 1; }
    if ! printf '%s\n' "$rps" | grep -qxF "$RPATH"; then
        int -add_rpath "$RPATH" "$BIN"
    fi

    # sign ad hoc: the libraries first, then the bundle (its main executable, and a seal of the
    # other files, the libraries included)
    for lib in "$LIBDIR"/*.dylib; do
        codesign -f -s - "$lib"
    done
    codesign -f -s - "$BUNDLE"
    codesign --verify --strict --verbose=1 "$BUNDLE"

    # check: every dependency is a system library or is bundled, and no rpath leads into $PREFIX
    status=0
    for f in "$BIN" "$LIBDIR"/*.dylib; do
        deps=$(deps_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        rps=$(rpaths_of "$f") || { echo "Error: otool failed on $f"; exit 1; }
        for dep in $deps; do
            case "$dep" in
            /usr/lib/*|/System/*) ;;
            @rpath/*) [ -f "$LIBDIR/${dep#@rpath/}" ] || { echo "Error: $f: $dep is not in $LIBDIR"; status=1; } ;;
            @loader_path/*) [ -f "$(dirname "$f")/${dep#@loader_path/}" ] || { echo "Error: $f: $dep not found"; status=1; } ;;
            *) echo "Error: $f depends on $dep, which is neither a system library nor bundled"; status=1 ;;
            esac
        done
        for rp in $rps; do
            case "$rp" in /*) echo "Error: $f has the absolute rpath $rp"; status=1 ;; esac
        done
    done
    [ $status = 0 ] || exit 1
    ;;

MINGW*|MSYS*)
    PREFIX=${MINGW_PREFIX:-/ucrt64}
    BINDIR=$(find "$BUNDLE/Contents" -maxdepth 1 -type d -name 'Win*' | head -1)
    BIN="$BINDIR/$NAME.ofx"
    [ -f "$BIN" ] || { echo "Error: $NAME.ofx not found in $BUNDLE/Contents/Win*"; exit 1; }
    lower() { tr '[:upper:]' '[:lower:]'; }
    # DLL names are case-insensitive: the lists are "lowercase-name actual-name"
    list_dlls() { find "$1" -maxdepth 1 -type f -iname '*.dll' -printf '%f\n' | awk '{print tolower($0), $0}'; }
    SYSDIR=$(cygpath -S)   # /c/Windows/System32
    list_dlls "$SYSDIR" > "$TMPD/system"
    list_dlls "$PREFIX/bin" > "$TMPD/prefix"
    actual() { awk -v n="$1" '$1 == n {print $2; exit}' "$2"; }

    # closure of the imports (objdump -p, not recursive) over the plug-in and the DLLs copied from
    # $PREFIX/bin, the system DLLs being leaves (the recursion of ntldd -R into C:\Windows reports
    # DLLs that some system DLLs import optionally as "not found"). An import that is in
    # $PREFIX/bin is bundled, even if System32 has a DLL with the same name: the manifest redirects
    # the loader to the bundled DLLs before it searches System32, and the build machine's System32
    # may hold third-party DLLs (e.g. zlib1.dll or libcrypto-3-x64.dll, installed by some software)
    # that the user's machine doesn't have, or in another version. Only an import that is not in
    # $PREFIX/bin but in System32 is a system DLL; the API sets (api-ms-*, ext-ms-*) are resolved
    # by the loader. Anything else is an error: it wouldn't load.
    # On a second run, the DLLs already copied are copied again.
    todo=( "$BIN" )
    : > "$TMPD/scanned"
    status=0
    while [ ${#todo[@]} -gt 0 ]; do
        f=${todo[0]}; todo=( "${todo[@]:1}" )
        imports=$(objdump -p "$f") || { echo "Error: objdump failed on $f"; exit 1; }
        for dll in $(printf '%s\n' "$imports" | awk '/DLL Name:/ {print $3}' | tr -d '\r' | lower | sort -u); do
            case "$dll" in api-ms-*|ext-ms-*) continue ;; esac
            grep -qxF "$dll" "$TMPD/scanned" && continue
            name=$(actual "$dll" "$TMPD/prefix")
            system=$(actual "$dll" "$TMPD/system")
            if [ -z "$name" ]; then
                [ -n "$system" ] && continue
                echo "Error: $(basename "$f") imports $dll, which is neither in $SYSDIR nor in $PREFIX/bin"
                status=1
                continue
            fi
            echo "$dll" >> "$TMPD/scanned"
            echo "* $name (imported by $(basename "$f"))"
            if [ -n "$system" ]; then
                echo "  Note: $SYSDIR/$system has the same name: the copy from $PREFIX/bin is bundled"
            fi
            cp "$PREFIX/bin/$name" "$BINDIR/"
            todo+=( "$BINDIR/$name" )
        done
    done
    [ $status = 0 ] || exit 1
    shopt -s nullglob
    dlls=( "$BINDIR"/*.dll )
    if [ ${#dlls[@]} = 0 ]; then
        echo "Error: no DLL from $PREFIX/bin is imported: is $BIN an MSYS2 build?"
        exit 1
    fi

    # strip, then embed a manifest that lists the DLLs (resource 2:
    # ISOLATIONAWARE_MANIFEST_RESOURCE_ID); stripping after embedding would remove it
    strip -s "$BIN"
    MT=$(command -v mt.exe || true)
    if [ -z "$MT" ]; then
        MT=$(find "/c/Program Files (x86)/Windows Kits/10/bin" -name mt.exe 2>/dev/null | grep -i '/x64/' | sort -V | tail -1 || true)
    fi
    [ -n "$MT" ] || { echo "Error: mt.exe (Windows SDK) not found"; exit 1; }
    MANIFEST="$TMPD/$NAME.ofx.manifest"
    {
        echo '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        echo '<assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">'
        echo "<assemblyIdentity name=\"$NAME.ofx\" version=\"1.0.0.0\" type=\"win32\" processorArchitecture=\"amd64\"/>"
        for dll in "${dlls[@]}"; do
            echo "<file name=\"$(basename "$dll")\"></file>"
        done
        echo '</assembly>'
    } > "$MANIFEST"
    cat "$MANIFEST"
    # mt.exe parses "-outputresource:file;2" itself: give it Windows paths, and keep MSYS2 from
    # converting the arguments
    MSYS2_ARG_CONV_EXCL='*' "$MT" -nologo -manifest "$(cygpath -w "$MANIFEST")" \
        "-outputresource:$(cygpath -w "$BIN");2"
    ;;

*)
    echo "$0: nothing to do on $(uname -s)"
    ;;
esac
echo "Bundled libraries in $BUNDLE:"
find "$BUNDLE/Contents" \( -name '*.dylib' -o -name '*.dll' \) | sort
