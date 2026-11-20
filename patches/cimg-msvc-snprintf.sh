#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that cimg_snprintf()
# always ends its output with a '\0' with MSVC. Usage: cimg-msvc-snprintf.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/507 (https://github.com/NatronGitHub/openfx-gmic/issues/111)
#
# With MSVC (and clang-cl), cimg_snprintf is cimg::_snprintf, whose body calls
# _vsnprintf(s,size,format,ap), meant to be cimg::_vsnprintf (_vsnprintf_s with _TRUNCATE, which
# always adds the '\0'). But cimg::_vsnprintf is declared after cimg::_snprintf, so the call
# resolves to the CRT's ::_vsnprintf, which adds no '\0' when it truncates (what CImg's comment just
# above warns about). G'MIC's 'input' command then leaves its 256-byte image title unterminated
# when the input values are longer (e.g. MontagePappusChain's math expression), and
# CImg<char>::string(title) reads past the end of the buffer: a crash when that reaches an unmapped
# page (native ARM64 builds, about 1 render in 8), garbage in the title otherwise (x64 too). MinGW
# and the other compilers use std::snprintf and are not affected.
#
# The patch declares cimg::_vsnprintf just before cimg::_snprintf, so that the call finds it.
# Running it again on a patched file does nothing, and so does running it on a CImg.h where the call
# no longer resolves to the CRT's function (cimg::_vsnprintf already declared before
# cimg::_snprintf, or another call: an upstream fix). The script fails in any other case (MSVC's
# cimg::_snprintf or cimg::_vsnprintf not found: e.g. after a GMICVER bump, then check what
# upstream changed, and update or remove this patch), or with CRLF line endings. Only POSIX sh,
# awk and wc are used, for Linux, macOS, MSYS2 and Git for Windows.
set -e
f="$1"

decl='inline int _vsnprintf(char *const s, const size_t size, const char *const format, va_list ap);'
marker="$decl // openfx-gmic patch: not the CRT's _vsnprintf (#111)"
first='inline int _snprintf(char *const s, const size_t size, const char *const format, ...) {'
call='const int result = _vsnprintf(s,size,format,ap);'
def='inline int _vsnprintf(char *const s, const size_t size, const char *const format, va_list ap) {'

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-msvc-snprintf.sh" >&2
  rm -f "$f.patched"
  exit 1
}

if [ "$(head -c 4096 "$f" | tr -d '\n' | wc -c)" != "$(head -c 4096 "$f" | tr -d '\r\n' | wc -c)" ]; then
  fail "CRLF line endings"
fi

# state <file>: "<markers> <MSVC _snprintf> <unqualified calls in it> <declarations before it>
# <MSVC _vsnprintf definitions>" (the MSVC versions: the first ones, in '#if cimg_OS==2 &&
# defined(_MSC_VER)', before the macOS ones of the same names)
state() {
  awk -v marker="$marker" -v decl="$decl" -v first="$first" -v call="$call" -v def="$def" '
    { t = $0; sub(/^[ \t]+/, "", t); sub(/[ \t]+$/, "", t) }
    t == marker { m++ }
    !seen && (t == decl || index(t, decl " ") == 1) { d++ }
    !seen && t == first { seen = 1; fn++; infn = 1; next }
    infn && t == call { c++ }
    infn && t == "}" { infn = 0 }
    seen && t == def { v++ }
    END { printf "%d %d %d %d %d\n", m, fn, c, d, v }' "$1"
}

set -- $(state "$f")
markers=$1 fns=$2 calls=$3 decls=$4 defs=$5
if [ "$markers" = 1 ] && [ "$fns" = 1 ] && [ "$decls" = 1 ]; then
  echo "$f: already patched (cimg_snprintf with MSVC, #111)"
  exit 0
fi
[ "$fns" = 1 ] || fail "MSVC's cimg::_snprintf not found ($fns)"
[ "$defs" -ge 1 ] || fail "MSVC's cimg::_vsnprintf not found after cimg::_snprintf"
if [ "$decls" != 0 ] || [ "$calls" = 0 ]; then
  echo "$f: not patched: cimg::_snprintf no longer calls the CRT's _vsnprintf (fixed upstream?), check and remove patches/cimg-msvc-snprintf.sh"
  exit 0
fi
[ "$calls" = 1 ] || fail "$calls calls of _vsnprintf in cimg::_snprintf, expected one"

awk -v marker="$marker" -v first="$first" '
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  !done && t == first { print ind marker; done = 1 }
  { print }' "$f" > "$f.patched"
set -- $(state "$f.patched")
[ "$1" = 1 ] && [ "$4" = 1 ] && [ "$3" = 1 ] || fail "the patched file is not as expected ($*)"
mv "$f.patched" "$f"
printf '%s: patched so that cimg_snprintf() always ends its output with a null character with MSVC (#111)\n' "$f"
