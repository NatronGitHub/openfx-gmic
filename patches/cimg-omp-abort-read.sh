#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the flag that
# stops CImg's OpenMP loops after an abort is read and written atomically (#136).
# Usage: cimg-omp-abort-read.sh <CImg.h>   (prints "already patched" on a patched file)
#
# With cimg_abort_test and OpenMP, a CImg function declares 'unsigned int _cimg_abort_go_openmp =
# 1' (_cimg_abort_init_openmp), shared by the threads of its parallel loop: a thread that catches
# CImgAbortException (or, in the math parser's fill loops, another CImgException) clears it with
# '#pragma omp atomic' (_cimg_abort_catch_openmp, _cimg_abort_catch_fill_openmp), while the other
# threads test it with a plain read before each iteration (_cimg_abort_try_openmp): a data race,
# undefined behaviour (C++ Core Guidelines CP.2), as an OpenMP atomic write doesn't make a plain
# read atomic. OpenMP's own fix, '#pragma omp atomic read', is a statement (the test is in an if
# condition) and needs OpenMP 3.1, while MSVC's /openmp is OpenMP 2.0.
# The patch defines _cimg_abort_go_load() and _cimg_abort_go_clear() before the macros, and uses
# them for the read and both writes: relaxed __atomic_load_n()/__atomic_store_n() with GCC, clang,
# clang-cl, MinGW and icx, MSVC's __iso_volatile_load32()/__iso_volatile_store32() (what MSVC's
# std::atomic uses for relaxed loads and stores), and with other compilers the code as before (a
# plain read, an OpenMP atomic write). Both sides use the same kind of access, so OpenMP atomics
# and compiler atomics are not mixed. Relaxed: the flag only stops the loop, and publishes no data
# (the threads join at the end of the parallel region). CImg.h without OpenMP or without
# cimg_abort_test is unchanged (the macros are then empty).
#
# Running it again on a patched file does nothing (it says so), and so does running it on a CImg.h
# where upstream made the accesses atomic (it says so too). The script fails in any other case (a
# line not found, or found more than once, or a mix of patched and unpatched lines: e.g. after a
# GMICVER bump, then check what upstream changed, and update or remove this patch). Only POSIX sh
# and awk are used, for Linux, macOS, MSYS2 and Git for Windows.
set -e
f="$1"
me="patches/$(basename "$0")"
tag='// openfx-gmic patch: atomic _cimg_abort_go_openmp'
anchor='// Define abort macros to be used with OpenMP.'
pairs="$f.pairs"
block="$f.block"
trap 'rm -f "$pairs" "$block" "$f.patched"' EXIT

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove $me" >&2
  exit 1
}

# $pairs: the lines to replace, without their indentation, each followed by its replacement (macro
# continuation lines: no tag, it would end the macro). $block: the lines inserted before $anchor,
# the first of which is $tag.
cat > "$pairs" <<'EOF'
#define _cimg_abort_try_openmp if (_cimg_abort_go_openmp) try
#define _cimg_abort_try_openmp if (_cimg_abort_go_load(_cimg_abort_go_openmp)) try
cimg_pragma_openmp(atomic) _cimg_abort_go_openmp&=0; \
_cimg_abort_go_clear(_cimg_abort_go_openmp); \
cimg_pragma_openmp(atomic) _cimg_abort_go_openmp&=0; }
_cimg_abort_go_clear(_cimg_abort_go_openmp); }
EOF
cat > "$block" <<EOF
$tag (patches/cimg-omp-abort-read.sh, #136):
// the threads of a parallel loop read the flag while one of them clears it.
#ifndef _cimg_abort_go_load
#if defined(__GNUC__) || defined(__clang__)
#define _cimg_abort_go_load(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define _cimg_abort_go_clear(x) __atomic_store_n(&(x),0U,__ATOMIC_RELAXED)
#elif defined(_MSC_VER)
#include <intrin.h>
#define _cimg_abort_go_load(x) ((unsigned int)__iso_volatile_load32((const volatile __int32*)&(x)))
#define _cimg_abort_go_clear(x) __iso_volatile_store32((volatile __int32*)&(x),0)
#else
#define _cimg_abort_go_load(x) (x)
#define _cimg_abort_go_clear(x) cimg_pragma_openmp(atomic) x&=0
#endif
#endif
// end of the openfx-gmic patch
EOF

# state <file>: "<sites> <unpatched> <patched> <bad> <block> <upstream>": the number of sites to
# patch; how many are found exactly once unpatched, how many exactly once patched; how many are
# found otherwise (missing, both, or more than once); how many times the inserted block is there
# (its first line); how many other lines use _cimg_abort_go_openmp and look atomic (with "atomic"
# or "load"). Lines are compared without their indentation.
state() {
  awk -v pairs="$pairs" -v block="$block" '
    BEGIN {
      while ((getline l < pairs) > 0) { if (n % 2 == 0) old[n / 2] = l; else rep[(n - 1) / 2] = l; n++ }
      n = int(n / 2)
      getline first < block
    }
    { t = $0; sub(/^[ \t]+/, "", t); known = 0 }
    { for (i = 0; i < n; i++) { if (t == old[i]) { o[i]++; known = 1 } if (t == rep[i]) { p[i]++; known = 1 } } }
    t == first { nb++ }
    !known && index(t, "_cimg_abort_go_openmp") && (index(t, "atomic") || index(t, "load")) { up++ }
    END {
      for (i = 0; i < n; i++) {
        if (o[i] == 1 && !p[i]) nu++
        else if (p[i] == 1 && !o[i]) np++
        else bad++
      }
      printf "%d %d %d %d %d %d\n", n, nu, np, bad, nb, up
    }' "$1"
}

set -- $(state "$f")
if [ "$3" = "$1" ] && [ "$5" = 1 ]; then
  echo "$f: already patched (_cimg_abort_go_openmp is read and written atomically)"
  exit 0
fi
if [ "$2" = 0 ] && [ "$3" = 0 ] && [ "$5" = 0 ] && [ "$6" != 0 ]; then
  echo "$f: not patched: _cimg_abort_go_openmp looks atomic already (fixed upstream?): check and remove $me"
  exit 0
fi
[ "$2" = "$1" ] && [ "$5" = 0 ] ||
  fail "expected each of the $1 accesses to _cimg_abort_go_openmp once, unpatched (found $2 unpatched, $3 patched, $4 missing or repeated; inserted block: $5)"
na=$(awk -v a="$anchor" '$0 == a { n++ } END { print n + 0 }' "$f")
[ "$na" = 1 ] || fail "expected the line '$anchor' once (found $na)"

awk -v pairs="$pairs" -v block="$block" -v anchor="$anchor" '
  BEGIN {
    while ((getline l < pairs) > 0) { if (n % 2 == 0) old[n / 2] = l; else rep[(n - 1) / 2] = l; n++ }
    n = int(n / 2)
    while ((getline l < block) > 0) b[nb++] = l
  }
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  $0 == anchor { for (i = 0; i < nb; i++) print b[i] }
  { for (i = 0; i < n; i++) if (t == old[i]) { print ind rep[i]; next } }
  { print }' "$f" > "$f.patched"
set -- $(state "$f.patched")
[ "$3" = "$1" ] && [ "$5" = 1 ] || fail "the patched file is not as expected ($*)"
mv "$f.patched" "$f"
echo "$f: patched so that _cimg_abort_go_openmp is read and written atomically"
