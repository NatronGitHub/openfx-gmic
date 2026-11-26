#!/bin/bash
# What openfx's Support/include/linuxCheckBundle.sh (run on the same bundles in ci.yml) doesn't
# check in a Linux GMIC.ofx:
# - it doesn't need libcurl (G'MIC downloads with the curl or wget command), FFTW (linked
#   statically with FFT=fftw3) or EGL; OpenGL (libGL, or GLVND's libOpenGL and libGLX, which
#   linuxCheckBundle.sh accepts as system libraries) only for the point handles of GPL builds
#   (SupportExt's PointsInteract; COMMERCIAL=1: none, GMIC_OFX/Makefile filters out -lGL); nor,
#   unless SHARED_DEPS=1, the shared
#   zlib or libpng (GMIC_STATIC_DEPS links them statically; linuxCheckBundle.sh allows a system
#   libz.so.1);
# - it exports only OfxGetNumberOfPlugins and OfxGetPlugin (the version script keeps the symbols
#   of the static libraries local);
# - its RUNPATH and RPATH, if present, are not empty and have no empty entry, which the loader
#   takes as the current directory (CMake pads a build-tree RUNPATH with ':' unless
#   BUILD_WITH_INSTALL_RPATH, which openfx's add_ofx_plugin sets by default:
#   OFX_PLUGIN_BUILD_WITH_INSTALL_RPATH). Check the installed bundle: it is the one shipped.
# SHARED_DEPS=1: a GMIC_STATIC_DEPS=OFF build (Natron's release builds, distribution packages),
# which may need the shared zlib and libpng (libz.so.1, libpng16.so.16).
# Usage: [SHARED_DEPS=1] [COMMERCIAL=1] check_linux_deps.sh <GMIC.ofx.bundle>
set -euo pipefail

if [ $# != 1 ]; then
    echo "Usage: $0 <GMIC.ofx.bundle>"
    exit 1
fi
bin=$(ls "$1"/Contents/Linux-*/GMIC.ofx)
status=0

needed=$(readelf -d "$bin" | sed -n -e 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
echo "NEEDED: $(echo $needed)"
for lib in $needed; do
    case "$lib" in
        libcurl*|libfftw3*|libEGL*) echo "FAIL: $bin needs $lib"; status=1 ;;
        libGL.so.1|libOpenGL.so.0|libGLX.so.0)
            [ "${COMMERCIAL:-0}" != 1 ] || { echo "FAIL: $bin needs $lib (COMMERCIAL builds have no point handles)"; status=1; } ;;
        libGL*|libOpenGL*) echo "FAIL: $bin needs $lib"; status=1 ;;
        libz.so.*|libpng*)
            [ "${SHARED_DEPS:-0}" = 1 ] ||
                { echo "FAIL: $bin needs $lib, which GMIC_STATIC_DEPS links statically"; status=1; } ;;
    esac
done

# RUNPATH and RPATH, each on its own (both may be present): no tag is fine; a tag with an empty
# value, or an empty entry in its value (a leading, trailing or double ':'), is an error
dynamic=$(readelf -d "$bin")
for tag in RUNPATH RPATH; do
    if ! printf '%s\n' "$dynamic" | grep -q "($tag)"; then
        echo "$tag: (none)"
        continue
    fi
    value=$(printf '%s\n' "$dynamic" | sed -n -e "s/.*($tag).*\\[\\(.*\\)\\]/\\1/p")
    echo "$tag: [$value]"
    if [ -z "$value" ]; then
        echo "FAIL: $bin has a $tag tag with an empty value ([]): not a binary made by cmake --install"; status=1
    else
        case ":$value:" in
            *::*) echo "FAIL: $bin has an empty $tag entry ($value), which the loader takes as the current directory"; status=1 ;;
        esac
    fi
done

# (without the version node, type A, and the @@OFX_1.0 version suffixes of the version script)
exports=$(nm -D --defined-only "$bin" | awk '$(NF-1) != "A" { sub(/@.*/, "", $NF); print $NF }' | LC_ALL=C sort | tr '\n' ' ')
echo "Exported symbols: $exports"
if [ "$exports" != "OfxGetNumberOfPlugins OfxGetPlugin " ]; then
    echo "FAIL: $bin exports other symbols than OfxGetNumberOfPlugins and OfxGetPlugin"
    status=1
fi

[ $status = 0 ] && echo "OK: $bin"
exit $status
