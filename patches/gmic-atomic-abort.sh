#!/bin/sh
# Patch gmic.h and gmic.cpp, extracted from the G'MIC sources by the top-level Makefile, so that
# G'MIC's abort flags are read and written atomically (#136).
# Usage: gmic-atomic-abort.sh <gmic.h|gmic.cpp>   (prints "already patched" on a patched file)
#
# The abort flag (the bool* p_is_abort of the gmic constructors and of gmic_libc, gmic::is_abort)
# is written by one thread and read by others: the caller sets it to stop a running interpreter
# (the plug-in from its render thread, gmic-qt from its GUI thread), while the interpreter's
# thread, those of 'parallel' and CImg's OpenMP loops (cimg_abort_test) poll it; G'MIC itself
# resets it when an interpreter starts and sets it on 'quit' and in the math parser's abort().
# gmic::is_abort_thread, which stops the threads of 'parallel', is set by the parent and sibling
# threads while the thread polls it. All are plain bools: a data race, undefined behaviour (C++
# Core Guidelines CP.2; volatile isn't a fix, CP.8): e.g. the compiler may hoist a test out of a
# loop, which then never sees the abort.
# The patch adds gmic_abort_load() and gmic_abort_store() to gmic.h (before '#ifndef gmic_core',
# so that library users get them too), relaxed atomic accesses to a bool: std::atomic_ref<bool>
# where the library has it (C++20), else __atomic_load_n()/__atomic_store_n() (GCC, clang,
# MinGW, clang-cl), else MSVC's __iso_volatile_load8()/__iso_volatile_store8() (what MSVC's
# std::atomic uses for relaxed loads and stores). Relaxed: the flags are stop requests and
# publish no data (the caller gets the results after joining G'MIC's thread). Every access to the
# flags in gmic.h (cimg_abort_test) and gmic.cpp goes through them; the bool* API and ABI are
# unchanged. The plug-in sets the flag with gmic_abort_store() (gmic_plugin_call.cpp).
#
# Running it again on a patched file does nothing (it says so), and so does running it on a file
# where upstream made the accesses atomic (it says so too). The script fails in any other case (a
# line not found, or found more than once, or a mix of patched and unpatched lines: e.g. after a
# GMICVER bump, then check what upstream changed, and update or remove this patch). Only POSIX sh
# and awk are used, for Linux, macOS, MSYS2 and Git for Windows.
set -e
f="$1"
me="patches/$(basename "$0")"
tag='// openfx-gmic patch: atomic abort flag'
pairs="$f.pairs"
block="$f.block"
trap 'rm -f "$pairs" "$block" "$f.patched"' EXIT

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the G'MIC sources changed: update or remove $me" >&2
  exit 1
}

# $pairs: the lines to replace, without their indentation, each followed by its replacement (the
# patched line is the replacement, then a space and $tag). $block (gmic.h): the lines inserted
# before $anchor, the first of which is $tag. $upstream: a pattern of the lines that upstream's own
# fix would change (then made atomic).
case "$(basename "$f")" in
gmic.h)
  what="cimg_abort_test reads the abort flag atomically"
  anchor='#ifndef gmic_core'
  upstream='define cimg_abort_test'
  cat > "$pairs" <<'EOF'
#define cimg_abort_test if (*gmic_is_abort) throw CImgAbortException()
#define cimg_abort_test if (::gmic_abort_load(gmic_is_abort)) throw CImgAbortException()
EOF
  cat > "$block" <<EOF
$tag (patches/gmic-atomic-abort.sh, #136).
// The abort flags (p_is_abort, is_abort_thread) are written and read by different threads (the
// caller's, the interpreter's, those of 'parallel' and of OpenMP): access them only through these
// (relaxed: a flag is a stop request, and publishes no data).
#include <atomic>
#if !defined(__cpp_lib_atomic_ref) && !defined(__GNUC__) && !defined(__clang__) && defined(_MSC_VER)
#include <intrin.h>
#endif
inline bool gmic_abort_load(const bool *const p_is_abort) {
#if defined(__cpp_lib_atomic_ref)
  return std::atomic_ref<bool>(*const_cast<bool*>(p_is_abort)).load(std::memory_order_relaxed);
#elif defined(__GNUC__) || defined(__clang__)
  return __atomic_load_n(p_is_abort,__ATOMIC_RELAXED);
#elif defined(_MSC_VER)
  return __iso_volatile_load8((const volatile __int8*)p_is_abort)!=0;
#else
#error "[gmic] *** Error *** No atomic access to a bool with this compiler (gmic_abort_load())."
#endif
}
inline void gmic_abort_store(bool *const p_is_abort, const bool value) {
#if defined(__cpp_lib_atomic_ref)
  std::atomic_ref<bool>(*p_is_abort).store(value,std::memory_order_relaxed);
#elif defined(__GNUC__) || defined(__clang__)
  __atomic_store_n(p_is_abort,value,__ATOMIC_RELAXED);
#elif defined(_MSC_VER)
  __iso_volatile_store8((volatile __int8*)p_is_abort,(__int8)value);
#endif
}
// end of the openfx-gmic patch

EOF
  ;;
gmic.cpp)
  what="G'MIC reads and writes its abort flags atomically"
  anchor=
  upstream='is_abort_thread'
  cat > "$pairs" <<'EOF'
*gmic_is_abort = true;
::gmic_abort_store(gmic_is_abort,true);
(*st.gmic_threads)[l].gmic_instance->is_abort_thread = true;
gmic_abort_store(&(*st.gmic_threads)[l].gmic_instance->is_abort_thread,true);
gmic_threads[l].gmic_instance->is_abort_thread = true;
gmic_abort_store(&gmic_threads[l].gmic_instance->is_abort_thread,true);
_is_abort = gmic_instance._is_abort;
_is_abort = gmic_abort_load(&gmic_instance._is_abort);
is_abort_thread = false;
gmic_abort_store(&is_abort_thread,false);
*is_abort = false;
gmic_abort_store(is_abort,false);
is_change = is_debug_info = is_quit = is_return = is_lbrace_command = is_abort_thread = false;
is_change = is_debug_info = is_quit = is_return = is_lbrace_command = false; gmic_abort_store(&is_abort_thread,false);
if (*is_abort || is_abort_thread) throw CImgAbortException();
if (gmic_abort_load(is_abort) || gmic_abort_load(&is_abort_thread)) throw CImgAbortException();
*is_abort = true;
gmic_abort_store(is_abort,true);
EOF
  : > "$block"
  ;;
*)
  echo "Error: $f: $me patches gmic.h and gmic.cpp only" >&2
  exit 1
  ;;
esac

# state <file>: "<sites> <unpatched> <patched> <bad> <block> <upstream>": the number of sites to
# patch; how many are found exactly once unpatched, how many exactly once patched; how many are
# found otherwise (missing, both, or more than once); how many times the inserted block is there
# (its first line); how many lines match $upstream and look atomic (with "atomic", "load" or
# "store"), without $tag. Lines are compared without their indentation.
state() {
  awk -v tag="$tag" -v upstream="$upstream" -v pairs="$pairs" -v block="$block" '
    BEGIN {
      while ((getline l < pairs) > 0) { if (n % 2 == 0) old[n / 2] = l; else rep[(n - 1) / 2] = l " " tag; n++ }
      n = int(n / 2)
      if ((getline first < block) <= 0) first = ""
    }
    { t = $0; sub(/^[ \t]+/, "", t) }
    { for (i = 0; i < n; i++) { if (t == old[i]) o[i]++; if (t == rep[i]) p[i]++ } }
    first != "" && t == first { nb++ }
    !index(t, tag) && index(t, upstream) && (index(t, "atomic") || index(t, "load") || index(t, "store")) { up++ }
    END {
      for (i = 0; i < n; i++) {
        if (o[i] == 1 && !p[i]) nu++
        else if (p[i] == 1 && !o[i]) np++
        else bad++
      }
      printf "%d %d %d %d %d %d\n", n, nu, np, bad, nb, up
    }' "$1"
}

nblock=0
[ -s "$block" ] && nblock=1
set -- $(state "$f")
if [ "$3" = "$1" ] && [ "$5" = "$nblock" ]; then
  echo "$f: already patched ($what)"
  exit 0
fi
if [ "$2" = 0 ] && [ "$3" = 0 ] && [ "$5" = 0 ] && [ "$6" != 0 ]; then
  echo "$f: not patched: the abort flags look atomic already (fixed upstream?): check and remove $me"
  exit 0
fi
[ "$2" = "$1" ] && [ "$5" = 0 ] ||
  fail "expected each of the $1 accesses to the abort flags once, unpatched (found $2 unpatched, $3 patched, $4 missing or repeated; inserted block: $5)"
if [ -n "$anchor" ]; then
  na=$(awk -v a="$anchor" '$0 == a { n++ } END { print n + 0 }' "$f")
  [ "$na" = 1 ] || fail "expected the line '$anchor' once (found $na)"
fi

awk -v tag="$tag" -v pairs="$pairs" -v block="$block" -v anchor="$anchor" '
  BEGIN {
    while ((getline l < pairs) > 0) { if (n % 2 == 0) old[n / 2] = l; else rep[(n - 1) / 2] = l; n++ }
    n = int(n / 2)
    while ((getline l < block) > 0) b[nb++] = l
  }
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  anchor != "" && $0 == anchor { for (i = 0; i < nb; i++) print b[i] }
  { for (i = 0; i < n; i++) if (t == old[i]) { print ind rep[i] " " tag; next } }
  { print }' "$f" > "$f.patched"
set -- $(state "$f.patched")
[ "$3" = "$1" ] && [ "$5" = "$nblock" ] || fail "the patched file is not as expected ($*)"
mv "$f.patched" "$f"
echo "$f: patched so that $what"
