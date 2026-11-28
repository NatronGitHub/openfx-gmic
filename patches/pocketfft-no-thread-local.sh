#!/bin/sh
# Patch pocketfft_hdronly.h (pocketfft's C++ header, at gmic_sources.txt's POCKETFFTCOMMIT) so
# that a plug-in that uses its thread pool can be unloaded on macOS. Usage:
# pocketfft-no-thread-local.sh <pocketfft_hdronly.h>
# https://github.com/NatronGitHub/openfx/issues/385
#
# macOS (dyld) never unloads an image that has thread-local variables (MH_HAS_TLV_DESCRIPTORS),
# and pocketfft's thread pool (built unless POCKETFFT_NO_MULTITHREADING) has two:
# threading::thread_id() and num_threads(), the share of the work of each task of thread_map(),
# which multi_iter reads. This script replaces them with the share of the task that the calling
# thread runs, set for the duration of that task by a share_scope: in a pthread key on POSIX,
# created by the first task and deleted when the binary is unloaded (or at exit, unless a task
# is still running), with no destructor, so that no thread calls the binary's code after it is
# unloaded; on Windows, which unloads DLLs that have thread-local variables, in a thread_local
# pointer. As openfx Support's OFX::MultiThread::ThreadScope (https://github.com/NatronGitHub/
# openfx/commit/0b19e90ed5aa2920cae5d155e05ec7bcc61901f7). Outside a task, the share is (0, 1), as
# the thread_local defaults were, and the FFT results are the same, bit for bit.
#
# It also removes the pthread_atfork() handlers that get_pool() registers with
# POCKETFFT_PTHREADS (not defined by openfx-gmic or openfx-misc): they can't be unregistered, so
# a fork() after the binary is unloaded would call unmapped code. Without them, a forked child
# that runs a multithreaded FFT before exec() would wait forever for workers that the child
# doesn't have; hosts fork only to exec().
#
# The script replaces the definitions of thread_id() and num_threads(), the two lines of
# thread_map() that set them, its call 'f();', the include of pthread.h and the pthread_atfork()
# block, each marked with "openfx-gmic patch". Running it again on a patched file does nothing,
# and so does running it on a pocketfft_hdronly.h that has neither thread_local nor
# pthread_atfork (fixed upstream). It fails in any other case: code that differs from that of
# POCKETFFTCOMMIT 4d865cba (e.g. after a POCKETFFTCOMMIT bump: then check what upstream changed,
# and update or remove this patch), or CRLF line endings. Lines are compared without their
# leading and trailing blanks. Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"
work="$f.patch-pocketfft"
marker="// openfx-gmic patch"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the pocketfft sources changed: update or remove patches/pocketfft-no-thread-local.sh (https://github.com/NatronGitHub/openfx/issues/385)" >&2
  rm -f "$f.patched" "$work"
  exit 1
}

# counts <file>: "<lines with thread_local> <pthread_atfork calls> <'#ifdef POCKETFFT_PTHREADS'>
# <'inline size_t &thread_id()'> <'inline size_t &num_threads()'> <'thread_id() = i;'>
# <'num_threads() = nthreads;'> <'f();'> <patch markers>" (thread_local and pthread_atfork
# outside // comments)
counts() {
  awk -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t); code = t; sub(/\/\/.*$/, "", code) }
    index(code, "thread_local") { tl++ }
    index(code, "pthread_atfork(") { af++ }
    t == "#ifdef POCKETFFT_PTHREADS" { pt++ }
    t == "inline size_t &thread_id()" { ti++ }
    t == "inline size_t &num_threads()" { nt++ }
    t == "thread_id() = i;" { si++ }
    t == "num_threads() = nthreads;" { sn++ }
    t == "f();" { fc++ }
    index(t, marker) { m++ }
    END { print tl + 0, af + 0, pt + 0, ti + 0, nt + 0, si + 0, sn + 0, fc + 0, m + 0 }' "$1"
}

case "$f" in
  *pocketfft_hdronly.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (download pocketfft_hdronly.h with LF line endings)"
fi

# the patched file: the Windows thread_local, no pthread_atfork, f() (after its share_scope), 5 markers
patched="1 0 0 0 0 0 0 1 5"
set -- `counts "$f"`
if [ "$*" = "$patched" ]; then
  echo "$f: already patched (no thread_local on POSIX, openfx#385)"
  exit 0
fi
if [ "$1" = 0 ] && [ "$2" = 0 ] && [ "$9" = 0 ]; then
  echo "$f: no thread_local or pthread_atfork: not patched (fixed upstream, openfx#385)"
  exit 0
fi
[ "$*" = "2 1 2 1 1 1 1 1 0" ] ||
  fail "expected pocketfft's 2 thread_local variables in 'inline size_t &thread_id()' and 'inline size_t &num_threads()', set by 'thread_id() = i;' and 'num_threads() = nthreads;' before one 'f();', and one pthread_atfork() in one of 2 '#ifdef POCKETFFT_PTHREADS'; found (thread_local, pthread_atfork, #ifdef, thread_id, num_threads, the 2 settings, f(), patch markers): $*"

# The replacement code, in sections "@@ <name>" (read by the awk below).
cat > "$work" <<'EOF'
@@ include
#ifndef _WIN32 // openfx-gmic patch: thread_map()'s share_scope, without thread_local
#  include <cerrno>
#  include <pthread.h>
#endif
@@ share
// openfx-gmic patch (patches/pocketfft-no-thread-local.sh): no thread_local on POSIX, because
// macOS never unloads a binary that has thread-local variables
// (https://github.com/NatronGitHub/openfx/issues/385). thread_id() and num_threads() are the
// share of the thread_map() task that the calling thread runs, set by its share_scope, or (0, 1)
// outside a task. POSIX: in a pthread key, created by the first task and deleted when the binary
// is unloaded, or at exit if no task is running (the key and the count are trivially
// destructible, so that a task can still use them afterwards); no key destructor, so that no
// thread calls this code after an unload. Hidden (GCC, clang), so that each binary has its own
// key, also with default visibility (not STB_GNU_UNIQUE on Linux). Windows unloads DLLs that
// have thread-local variables: a thread_local pointer.
struct thread_share { size_t id, n; };
#ifdef _WIN32
inline const thread_share *&share_slot()
  {
  static thread_local const thread_share *share_=nullptr;
  return share_;
  }
inline const thread_share *current_share() { return share_slot(); }
class share_scope
  {
    const thread_share *previous_;
  public:
    explicit share_scope(const thread_share *share): previous_(share_slot())
      { share_slot() = share; }
    ~share_scope() { share_slot() = previous_; }
    share_scope(const share_scope &) = delete;
    share_scope &operator=(const share_scope &) = delete;
  };
#else
#if defined(__GNUC__)
#  define POCKETFFT_SHARE_HIDDEN __attribute__((visibility("hidden")))
#else
#  define POCKETFFT_SHARE_HIDDEN
#endif
POCKETFFT_SHARE_HIDDEN inline pthread_key_t &share_key()
  {
  static pthread_key_t key_;
  return key_;
  }
// the tasks running, on all threads
POCKETFFT_SHARE_HIDDEN inline std::atomic<size_t> &live_shares()
  {
  static std::atomic<size_t> live_{0};
  return live_;
  }
[[noreturn]] inline void share_error(int err, const char *what)
  {
  if (err==ENOMEM) POCKETFFT_THROW(std::bad_alloc());
  POCKETFFT_THROW(std::runtime_error(what));
  }
struct POCKETFFT_SHARE_HIDDEN share_key_owner
  {
  share_key_owner()
    {
    int err = pthread_key_create(&share_key(), nullptr);
    if (err!=0) share_error(err, "pocketfft: pthread_key_create failed");
    }
  ~share_key_owner()
    {
    if (live_shares().load(std::memory_order_acquire)==0)
      pthread_key_delete(share_key());
    }
  share_key_owner(const share_key_owner &) = delete;
  share_key_owner &operator=(const share_key_owner &) = delete;
  };
inline const thread_share *current_share()
  {
  // no task running (on any thread): the key may not exist yet
  if (live_shares().load(std::memory_order_acquire)==0) return nullptr;
  return static_cast<const thread_share *>(pthread_getspecific(share_key()));
  }
class share_scope
  {
    const thread_share *previous_;
    POCKETFFT_SHARE_HIDDEN static const thread_share *push(const thread_share *share)
      {
      static const share_key_owner owner; // (thread-safe, and tried again if it throws)
      (void)owner;
      live_shares().fetch_add(1, std::memory_order_acq_rel); // (from here, current_share() reads the key)
      auto *previous = static_cast<const thread_share *>(pthread_getspecific(share_key()));
      int err = pthread_setspecific(share_key(), share);
      if (err!=0)
        {
        live_shares().fetch_sub(1, std::memory_order_release);
        share_error(err, "pocketfft: pthread_setspecific failed");
        }
      return previous;
      }
  public:
    explicit share_scope(const thread_share *share): previous_(push(share)) {}
    ~share_scope()
      {
      pthread_setspecific(share_key(), previous_); // (can't fail: the thread's slot exists)
      live_shares().fetch_sub(1, std::memory_order_release);
      }
    share_scope(const share_scope &) = delete;
    share_scope &operator=(const share_scope &) = delete;
  };
#undef POCKETFFT_SHARE_HIDDEN
#endif
inline size_t thread_id()
  {
  const thread_share *share = current_share();
  return share ? share->id : 0;
  }
inline size_t num_threads()
  {
  const thread_share *share = current_share();
  return share ? share->n : 1;
  }
@@ atfork
  // openfx-gmic patch: no pthread_atfork() handlers (POCKETFFT_PTHREADS): they can't be removed,
  // and would be called after the binary is unloaded (see thread_id())
@@ set
      const thread_share share{i, nthreads}; // openfx-gmic patch: thread_id(), num_threads()
@@ call
        const share_scope scope(&share); // openfx-gmic patch: for the duration of f()
        f();
EOF

# Each replaced block is matched line by line (without blanks), from its first line to its
# last: 'thread_id()' and 'num_threads()' (5 lines each, replaced by "share"), the
# POCKETFFT_PTHREADS include (3 lines, "include") and the pthread_atfork() block (11 lines,
# "atfork"), the two settings ("set"), and 'f();' ("call").
awk -v work="$work" '
BEGIN {
  while ((getline line < work) > 0) {
    if (line ~ /^@@ /) { s = substr(line, 4); continue }
    block[s] = block[s] line "\n"
  }
  close(work)
  # the expected lines of each multi-line block, trimmed
  n = split("inline size_t &thread_id()|{|static thread_local size_t thread_id_=0;|return thread_id_;|}|inline size_t &num_threads()|{|static thread_local size_t num_threads_=1;|return num_threads_;|}", tls, "|")
  m = split("#ifdef POCKETFFT_PTHREADS|static std::once_flag f;|std::call_once(f,|[]{|pthread_atfork(|+[]{ get_pool().shutdown(); },  // prepare|+[]{ get_pool().restart(); },   // parent|+[]{ get_pool().restart(); }    // child|);|});|#endif", af, "|")
  k = split("#ifdef POCKETFFT_PTHREADS|#  include <pthread.h>|#endif", inc, "|")
}
function bad(what) { print "unexpected code " what " at line " NR ": " $0 > "/dev/stderr"; failed = 1; exit 1 }
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
mode == "tls" { if (t != tls[++i]) bad("in thread_id()/num_threads()"); if (i == n) mode = ""; next }
mode == "af" { if (t != af[++i]) bad("in the pthread_atfork() block"); if (i == m) mode = ""; next }
mode == "inc" { if (t != inc[++i]) bad("in the pthread.h include"); if (i == k) mode = ""; next }
t == "inline size_t &thread_id()" { mode = "tls"; i = 1; printf "%s", block["share"]; next }
t == "#ifdef POCKETFFT_PTHREADS" {
  if (!seen_inc) { seen_inc = 1; mode = "inc"; i = 1; printf "%s", block["include"]; next }
  mode = "af"; i = 1; printf "%s", block["atfork"]; next
}
t == "thread_id() = i;" { printf "%s", block["set"]; next }
t == "num_threads() = nthreads;" { next }
t == "f();" { printf "%s", block["call"]; next }
{ print }
END { if (!failed && mode != "") { print "unexpected end of file in a replaced block" > "/dev/stderr"; exit 1 } }
' "$f" > "$f.patched" || fail "the code to patch is not as expected"
rm -f "$work"

set -- `counts "$f.patched"`
[ "$*" = "$patched" ] || fail "the patch did not apply (counts: $*)"
# the line count: -10 + 110 (share), -3 + 4 (include), -11 + 2 (atfork), -2 + 1 (set), -1 + 2 (call)
added=`awk 'NR == FNR { n++; next } { p++ } END { print p - n }' "$f" "$f.patched"`
[ "$added" = 92 ] || fail "the patch added $added lines, not 92"
mv "$f.patched" "$f"
echo "$f: patched: no thread_local on POSIX, no pthread_atfork (openfx#385)"
