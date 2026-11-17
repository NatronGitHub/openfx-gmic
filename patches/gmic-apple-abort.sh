#!/bin/sh
# Patch the G'MIC sources extracted by the top-level Makefile (gmic.h and gmic.cpp) so that
# G'MIC's abort support (cimg_use_abort) also works on macOS. Usage: gmic-apple-abort.sh <file>
#
# G'MIC 4.0.7 defines cimg_abort_init/cimg_abort_test only when
#   defined(cimg_use_abort) && !defined(__MACOSX__) && !defined(__APPLE__)
# so on macOS CImg's _cimg_abort_try_openmp/_cimg_abort_catch_*_openmp macros are empty: an
# exception thrown inside an OpenMP region (e.g. a math parser error in 'fill') is not caught,
# and terminates the host instead of being reported as a G'MIC error. CImg's loops also ignore
# the abort flag passed to G'MIC (p_is_abort). The exclusion comes with get_tid() returning
# getpid() on Apple, so that gmic::current_is_abort() cannot tell concurrent interpreters
# apart: Apple uses pthread_self() instead, as NetBSD does.
# https://github.com/NatronGitHub/openfx-gmic/issues/41
# Upstream report, with the same fix: https://github.com/GreycLab/gmic/issues/219
# One addition to the upstream diff: gmic.cpp includes <pthread.h> on Apple before get_tid(),
# which otherwise relies on gmic.h including it when gmic_is_parallel is defined.
#
# The changes only affect Apple builds: on Linux and Windows, the preprocessed code is the same.
# The script fails if the source does not contain the expected code (e.g. after a GMICVER bump):
# then check whether upstream fixed it, and update or remove this patch. Running it again on a
# patched file does nothing. Only POSIX sh, sed and grep are used (no sed -i, whose syntax
# differs between GNU and BSD sed), for Linux, macOS and MSYS2.
set -e
f="$1"

guard_old='#if defined(cimg_use_abort) && !defined(__MACOSX__) && !defined(__APPLE__)'
guard_new='#if defined(cimg_use_abort)'
guard_sed='s/^#if defined(cimg_use_abort) \&\& !defined(__MACOSX__) \&\& !defined(__APPLE__)$/#if defined(cimg_use_abort)/'

# get_tid() in gmic.cpp, with the comment before it, before and after the patch
tid_old='// Return current thread id.
inline void* get_tid() {
#if defined(__MACOSX__) || defined(__APPLE__)
  void* tid = (void*)(cimg_ulong)getpid();
#elif cimg_OS==1
#if defined(__NetBSD__) || cimg_use_pthread==1
  void* tid = (void*)(cimg_ulong)pthread_self();
#else
  void* tid = (void*)(cimg_ulong)syscall(SYS_gettid);
#endif
#elif cimg_OS==2
  void* tid = (void*)(cimg_ulong)GetCurrentThreadId();
#else
  void* tid = (void*)0;
#endif // #if defined(__MACOSX__) || defined(__APPLE__)
  return tid;
}'
tid_new='// Return current thread id.
#if defined(__MACOSX__) || defined(__APPLE__)
#include <pthread.h> // openfx-gmic: for pthread_self() (not in GreycLab/gmic#219)
#endif
inline void* get_tid() {
#if cimg_OS==1
#if defined(__NetBSD__) || defined(__MACOSX__) || defined(__APPLE__) || cimg_use_pthread==1
  void* tid = (void*)(cimg_ulong)pthread_self();
#else
  void* tid = (void*)(cimg_ulong)syscall(SYS_gettid);
#endif
#elif cimg_OS==2
  void* tid = (void*)(cimg_ulong)GetCurrentThreadId();
#else
  void* tid = (void*)0;
#endif // #if cimg_OS==1
  return tid;
}'
# sed commands applied to the lines of get_tid() (and the comment before it) only
tid_range='/^\/\/ Return current thread id\.$/,/^}$/'
tid_sed="${tid_range}{
s/^\\/\\/ Return current thread id\\.\$/&\\
#if defined(__MACOSX__) || defined(__APPLE__)\\
#include <pthread.h> \\/\\/ openfx-gmic: for pthread_self() (not in GreycLab\\/gmic#219)\\
#endif/
/^#if defined(__MACOSX__) || defined(__APPLE__)\$/d
/^  void\\* tid = (void\\*)(cimg_ulong)getpid();\$/d
s/^#elif cimg_OS==1\$/#if cimg_OS==1/
s/^#if defined(__NetBSD__) || cimg_use_pthread==1\$/#if defined(__NetBSD__) || defined(__MACOSX__) || defined(__APPLE__) || cimg_use_pthread==1/
s/^#endif \\/\\/ #if defined(__MACOSX__) || defined(__APPLE__)\$/#endif \\/\\/ #if cimg_OS==1/
}"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the G'MIC sources changed: update or remove patches/gmic-apple-abort.sh (openfx-gmic issue #41)" >&2
  rm -f "$f.patched"
  exit 1
}

# count <file> <line>: number of lines equal to <line>
count() { grep -c -x -F -e "$2" "$1" || true; }
# get_tid <file>: the get_tid() function
get_tid() { sed -n "${tid_range}p" "$1"; }

case "$f" in
  *gmic.h) tid=no;;
  *gmic.cpp) tid=yes;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

# already patched: nothing to do
if [ "`count "$f" "$guard_old"`" = 0 ] && [ "`count "$f" "$guard_new"`" = 1 ] &&
   { [ $tid = no ] || [ "`get_tid "$f"`" = "$tid_new" ]; }; then
  echo "$f: already patched (issue #41)"
  exit 0
fi

# the code to patch must be found exactly
[ "`count "$f" "$guard_old"`" = 1 ] && [ "`count "$f" "$guard_new"`" = 0 ] || fail "expected exactly one line '$guard_old'"
if [ $tid = yes ]; then
  [ "`get_tid "$f"`" = "$tid_old" ] || fail "get_tid() is not the expected code"
  sed -e "$guard_sed" -e "$tid_sed" "$f" > "$f.patched" || fail "sed failed"
  [ "`get_tid "$f.patched"`" = "$tid_new" ] || fail "the get_tid() patch did not apply"
  expected_lines=$((`wc -l < "$f"` + 1))
else
  sed -e "$guard_sed" "$f" > "$f.patched" || fail "sed failed"
  expected_lines=`wc -l < "$f"`
fi
[ "`count "$f.patched" "$guard_new"`" = 1 ] && [ "`count "$f.patched" "$guard_old"`" = 0 ] || fail "the cimg_use_abort patch did not apply"
[ "`wc -l < "$f.patched"`" -eq "$expected_lines" ] || fail "the patch changed other lines"
mv "$f.patched" "$f"
echo "$f: patched for G'MIC abort support on macOS (issue #41)"
