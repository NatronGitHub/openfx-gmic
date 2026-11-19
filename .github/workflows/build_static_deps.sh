#!/bin/bash
# Build static zlib and libpng archives with -fPIC, for linking into GMIC.ofx on Linux
# (GMIC_STATIC_DEPS): distributions' archives are often built without -fPIC (e.g. Ubuntu's
# libpng16.a), and cannot go into a shared object. The tarballs are sha256-checked.
# Usage: build_static_deps.sh <prefix>
# then build with PKG_CONFIG_PATH=<prefix>/lib/pkgconfig (printed at the end).
set -euo pipefail

if [ $# != 1 ]; then
    echo "Usage: $0 <prefix>"
    exit 1
fi
mkdir -p "$1"
PREFIX=$(cd "$1" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)

ZLIB=zlib-1.3.1
ZLIB_URL=https://github.com/madler/zlib/releases/download/v1.3.1/$ZLIB.tar.gz
ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
LIBPNG=libpng-1.6.59
LIBPNG_URL=https://download.sourceforge.net/libpng/$LIBPNG.tar.xz
LIBPNG_SHA256=d80dd2a38a37f803cb9b6ac7b14bd6e74ddc3b654780a8380bdf93523fdb4389

fetch() { # fetch <url> <sha256> <file>
    curl -f --retry 5 --retry-all-errors -L -s -S -o "$WORK/$3" "$1"
    echo "$2  $WORK/$3" | sha256sum -c -
}

fetch "$ZLIB_URL" "$ZLIB_SHA256" "$ZLIB.tar.gz"
tar -C "$WORK" -xzf "$WORK/$ZLIB.tar.gz"
(cd "$WORK/$ZLIB" && CFLAGS="-O2 -fPIC" ./configure --static --prefix="$PREFIX" && make -j"$JOBS" && make install)

fetch "$LIBPNG_URL" "$LIBPNG_SHA256" "$LIBPNG.tar.xz"
tar -C "$WORK" -xJf "$WORK/$LIBPNG.tar.xz"
(cd "$WORK/$LIBPNG" && ./configure --prefix="$PREFIX" --disable-shared --enable-static --with-pic \
     CFLAGS="-O2" CPPFLAGS="-I$PREFIX/include" LDFLAGS="-L$PREFIX/lib" &&
 make -j"$JOBS" && make install)

ls "$PREFIX/lib/libz.a" "$PREFIX/lib/libpng16.a"
echo "PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig"
