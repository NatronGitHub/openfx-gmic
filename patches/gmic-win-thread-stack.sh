#!/bin/sh
# Patch gmic.cpp, extracted from the G'MIC sources, so that the threads of G'MIC's 'parallel'
# command reserve an 8 MiB stack on Windows, as G'MIC already does on macOS and with clang
# (pthread_attr_setstacksize). G'MIC creates them with CreateThread(0,0,...): the stack reserve of
# the host executable, 1 MiB with MSVC's linker (2 MiB with MinGW's), which some filters overflow
# (STATUS_STACK_OVERFLOW: e.g. VintageStyle, PaperCuts, 3DConversion in an MSVC-linked host).
# Reported upstream: https://github.com/GreycLab/gmic/issues/226
# Usage: gmic-win-thread-stack.sh <gmic.cpp>   (prints "already patched" on a patched file)
# Running it again on a patched file does nothing; it fails if the code changed (e.g. after a
# GMICVER bump: then check what upstream changed, and update or remove this patch). Only POSIX sh,
# grep and awk are used, for Linux, macOS, MSYS2 and Git for Windows.
set -e
f="$1"
me="patches/$(basename "$0")"
old1='_gmic_threads[l].thread_id = CreateThread(0,0,(LPTHREAD_START_ROUTINE)gmic_parallel<T>,'
new1='_gmic_threads[l].thread_id = CreateThread(0,(SIZE_T)8*1024*1024,(LPTHREAD_START_ROUTINE)gmic_parallel<T>, // openfx-gmic patch: 8 MiB stack'
old2='(void*)&_gmic_threads[l],0,0);'
new2='(void*)&_gmic_threads[l],STACK_SIZE_PARAM_IS_A_RESERVATION,0);'

# pairs <first line> <second line>: how many times the call (its two lines, without their
# indentation) is in the file
pairs() {
  awk -v a="$1" -v b="$2" '
    { t = $0; sub(/^[ \t]+/, "", t) }
    prev == a && t == b { n++ }
    { prev = t }
    END { print n + 0 }' "$f"
}

if [ "$(pairs "$new1" "$new2")" = 1 ] && [ "$(pairs "$old1" "$old2")" = 0 ]; then
  echo "$f: already patched (threads of 'parallel' with an 8 MiB stack on Windows)"
  exit 0
fi
n=$(pairs "$old1" "$old2")
if [ "$n" != 1 ] || [ "$(pairs "$new1" "$new2")" != 0 ]; then
  echo "Error: $f: expected the CreateThread call of 'parallel' once, unpatched (found $n): update or remove $me" >&2
  exit 1
fi
awk -v o1="$old1" -v n1="$new1" -v o2="$old2" -v n2="$new2" '
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  t == o1 { print ind n1; next1 = 1; next }
  next1 { next1 = 0; if (t == o2) { print ind n2; done = 1; next } else { exit 2 } }
  { print }
  END { if (!done) exit 2 }' "$f" > "$f.patched" ||
  { rm -f "$f.patched"; echo "Error: $f: unexpected CreateThread call: update $me" >&2; exit 1; }
mv "$f.patched" "$f"
echo "$f: patched so that the threads of 'parallel' have an 8 MiB stack on Windows"
