#!/bin/sh
# Patch pocketfft_hdronly.h (pocketfft's C++ header, at gmic_sources.txt's POCKETFFTCOMMIT) so
# that its thread pool has no thread-local variables, which macOS never unloads. Usage:
# pocketfft-no-thread-local.sh <pocketfft_hdronly.h>
# https://github.com/NatronGitHub/openfx/issues/385, https://github.com/mreineck/pocketfft/pull/3
#
# macOS (dyld) never unloads an image that has thread-local variables (MH_HAS_TLV_DESCRIPTORS),
# and pocketfft's thread pool (built unless POCKETFFT_NO_MULTITHREADING) has two:
# threading::thread_id() and num_threads(), each task's share of the work, which thread_map()
# sets and multi_iter's constructor reads. As in upstream's own fix (pocketfft PR #3, closed
# unmerged), thread_map() passes the share to its function instead, f(i, nthreads) (f(0, 1) when
# single-threaded), the three lambdas take it as (myshare, nshares) and give it to multi_iter,
# whose constructor takes (..., nshares, myshare), #3's order. No per-thread state is left, and
# the FFT results are the same, bit for bit. The pthread_atfork() handlers (POCKETFFT_PTHREADS,
# defined neither here nor in openfx-misc) are left as they are.
#
# Running the script again on a patched file does nothing, and so does running it on a
# pocketfft_hdronly.h that has neither thread_id() nor thread_local (fixed upstream). It fails in
# any other case: code that differs from that of POCKETFFTCOMMIT 4d865cba (e.g. after a
# POCKETFFTCOMMIT bump: then check what upstream changed, and update or remove this patch), or
# CRLF line endings. Lines are compared without their leading and trailing blanks. Only POSIX
# sh and awk are used, for Linux, macOS and MSYS2.
set -e
f="$1"
marker="// openfx-gmic patch: the share as parameters, not thread-local variables (https://github.com/mreineck/pocketfft/pull/3)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the pocketfft sources changed: update or remove patches/pocketfft-no-thread-local.sh (https://github.com/NatronGitHub/openfx/issues/385)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: the number of lines (trimmed) of each kind, the old ones first:
# "<thread_local> <thread_id() or num_threads()> <'f();' forms> <old multi_iter constructor>
# <'[&] {'> <old 'multi_iter<vlen> it(...)'> | <markers> <'f(0, 1);' forms> <'f(i, nthreads);'>
# <'[&](size_t myshare, size_t nshares) {'> <new 'multi_iter<vlen> it(...)'>"
counts() {
  awk -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    index(t, "thread_local") { tl++ }
    index(t, "thread_id()") || index(t, "num_threads()") { ti++ }
    t == "{ f(); }" || t == "{ f(); return; }" || t == "f();" { f0++ }
    t == "multi_iter(const arr_info &iarr_, const arr_info &oarr_, size_t idim_)" { mc++ }
    t == "[&] {" { la++ }
    t ~ /^multi_iter<vlen> it\([a-z]+, out, (axis|axes\[iax\])\);$/ { mi++ }
    t == marker { m++ }
    t == "{ f(0, 1); }" || t == "{ f(0, 1); return; }" { f1++ }
    t == "f(i, nthreads);" { fi++ }
    t == "[&](size_t myshare, size_t nshares) {" { nl++ }
    t ~ /^multi_iter<vlen> it\([a-z]+, out, (axis|axes\[iax\]), nshares, myshare\);$/ { nm++ }
    END { print tl + 0, ti + 0, f0 + 0, mc + 0, la + 0, mi + 0, "|", m + 0, f1 + 0, fi + 0, nl + 0, nm + 0 }' "$1"
}

case "$f" in
  *pocketfft_hdronly.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || { echo "Error: $f: file not found" >&2; exit 1; }
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (download pocketfft_hdronly.h with LF line endings)"
fi

patched="0 0 0 0 0 0 | 1 2 1 3 3"
c=`counts "$f"`
if [ "$c" = "$patched" ]; then
  echo "$f: already patched (no thread_local, openfx#385)"
  exit 0
fi
case "$c" in
  "0 0 "*"| 0 "*) echo "$f: no thread_id() or thread_local: not patched (fixed upstream, openfx#385)"; exit 0;;
esac
# 2 thread_local; thread_id() and num_threads(): 4 definitions (2 single-threaded), 2 settings
# and 2 reads
[ "$c" = "2 8 3 1 3 3 | 0 0 0 0 0" ] ||
  fail "expected pocketfft 4d865cba's thread_local thread_id() and num_threads(), set by thread_map() and read by multi_iter's constructor, 3 'f();', 3 '[&] {' and 3 'multi_iter<vlen> it(...)'; found the counts '$c' (see counts() in the script)"

awk -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
# the rest of the thread_local definitions, from "inline size_t &thread_id()"
skip > 0 {
  if (t != want[++j]) { print "unexpected line " NR ": " $0 > "/dev/stderr"; bad = 1; exit 1 }
  skip--
  next
}
t == "inline size_t &thread_id()" {
  skip = split("{|static thread_local size_t thread_id_=0;|return thread_id_;|}|inline size_t &num_threads()|{|static thread_local size_t num_threads_=1;|return num_threads_;|}", want, "|")
  j = 0
  next
}
t == "constexpr inline size_t thread_id() { return 0; }" || t == "constexpr inline size_t num_threads() { return 1; }" { next }
t == "thread_id() = i;" || t == "num_threads() = nthreads;" { next }
t == "auto nshares = threading::num_threads();" || t == "auto myshare = threading::thread_id();" { next }
t == "{ f(); }" || t == "{ f(); return; }" { sub(/f\(\);/, "f(0, 1);") }
t == "f();" { sub(/f\(\);/, "f(i, nthreads);") }
t == "[&] {" { sub(/\[&\] \{/, "[\\&](size_t myshare, size_t nshares) {") }
t ~ /^multi_iter<vlen> it\(/ { sub(/\);[ \t]*$/, ", nshares, myshare);") }
t == "multi_iter(const arr_info &iarr_, const arr_info &oarr_, size_t idim_)" {
  ind = $0; sub(/[^ \t].*$/, "", ind)
  print ind marker
  print ind "multi_iter(const arr_info &iarr_, const arr_info &oarr_, size_t idim_,"
  print ind "  size_t nshares, size_t myshare)"
  next
}
{ print }
END { if (!bad && skip) { print "unexpected end of file" > "/dev/stderr"; exit 1 } }
' "$f" > "$f.patched" || fail "the code to patch is not as expected"

c=`counts "$f.patched"`
[ "$c" = "$patched" ] || fail "the patch did not apply (counts '$c', expected '$patched')"
mv "$f.patched" "$f"
echo "$f: patched: thread_map() passes each task's share, no thread_local (openfx#385, pocketfft#3)"
