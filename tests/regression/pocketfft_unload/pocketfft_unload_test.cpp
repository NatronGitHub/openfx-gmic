/*
  pocketfft_unload_test MODULE: regression test of patches/pocketfft-no-thread-local.sh
  (https://github.com/NatronGitHub/openfx/issues/385, https://github.com/mreineck/pocketfft/pull/3).

  pocketfft's thread pool had two thread-local variables (threading::thread_id() and
  num_threads()), and macOS (dyld) never unloads an image that has any (MH_HAS_TLV_DESCRIPTORS):
  GMIC.ofx, which uses the pool through CImg's FFT, could not be unloaded. The patch passes each
  task's share of the work as parameters instead, as upstream's pull request #3 did.

  MODULE (pocketfft_fft_module.cpp) includes the build's patched pocketfft_hdronly.h, and is
  built and exported as GMIC.ofx is (hidden visibility, one exported function). This
  program loads it, runs its checks (multithreaded FFTs bit-identical to single-threaded ones,
  round trips), checks that it has no thread-local variables (macOS: no MH_HAS_TLV_DESCRIPTORS;
  Linux: no PT_TLS segment, which glibc unloads, but which reveals the same variables, so that the
  required Linux job guards the regression too), closes it, and checks that it is gone (dlopen()
  with RTLD_NOLOAD doesn't find it), which also runs the pool's destructor (joining its worker
  threads) inside dlclose(). Twice: the second load creates a new pool. Exits with 0 if all
  passed. tests/regression/CMakeLists.txt also runs it on the module built with the unpatched
  header, which must fail. Not on Windows, where pocketfft is single-threaded in GMIC.ofx
  (CMakeLists.txt).
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // dlinfo(), RTLD_DI_LINKMAP (glibc)
#endif
#include <cstdio>
#include <cstdlib>
#include <string>

#include <dlfcn.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#elif defined(__linux__)
#include <link.h>
#endif

namespace {

typedef int (*Run)(char*, size_t);

#if defined(__linux__)
// whether the loaded object at base has a PT_TLS segment (thread-local variables)
struct TlsQuery {
  ElfW(Addr) base;
  bool found;
  bool tls;
};

int findTls(dl_phdr_info* info, size_t, void* data)
{
  TlsQuery* q = static_cast<TlsQuery*>(data);
  if (info->dlpi_addr != q->base) {
    return 0;
  }
  q->found = true;
  for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
    q->tls = q->tls || info->dlpi_phdr[i].p_type == PT_TLS;
  }
  return 1;
}
#endif

// returns whether the checks passed
bool loadRunUnload(const char* path, int round)
{
  void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    std::printf("FAILED: round %d: dlopen: %s\n", round, dlerror());
    return false;
  }
  Run run = reinterpret_cast<Run>(dlsym(handle, "pocketfft_unload_test_run"));
  if (!run) {
    std::printf("FAILED: round %d: %s has no pocketfft_unload_test_run\n", round, path);
    dlclose(handle);
    return false;
  }
  static char log[4096];
  bool ok = run(log, sizeof(log)) == 0;
  std::printf("round %d:\n%s", round, log);
#ifdef __APPLE__
  Dl_info info;
  if (!dladdr(reinterpret_cast<void*>(run), &info) || !info.dli_fbase) {
    std::printf("FAILED: round %d: dladdr\n", round);
    dlclose(handle);
    return false;
  }
  if (static_cast<const struct mach_header_64*>(info.dli_fbase)->flags & MH_HAS_TLV_DESCRIPTORS) {
    std::printf("FAILED: round %d: %s has thread-local variables (MH_HAS_TLV_DESCRIPTORS): macOS never unloads it\n",
                round, path);
    ok = false;
  }
#elif defined(__linux__)
  link_map* lm = nullptr;
  if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm) {
    std::printf("FAILED: round %d: dlinfo: %s\n", round, dlerror());
    dlclose(handle);
    return false;
  }
  TlsQuery q = {lm->l_addr, false, false};
  dl_iterate_phdr(findTls, &q);
  if (!q.found) {
    std::printf("FAILED: round %d: %s not found by dl_iterate_phdr()\n", round, path);
    ok = false;
  } else if (q.tls) {
    std::printf("FAILED: round %d: %s has thread-local variables (a PT_TLS segment), which macOS never unloads\n",
                round, path);
    ok = false;
  }
#endif
  if (dlclose(handle) != 0) {
    std::printf("FAILED: round %d: dlclose: %s\n", round, dlerror());
    return false;
  }
  if (void* again = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD)) {
    std::printf("FAILED: round %d: %s is still loaded after dlclose()\n", round, path);
    dlclose(again);
    ok = false;
#if defined(__linux__)
    // (diagnostic: glibc never unloads a module that defines STB_GNU_UNIQUE symbols, or is NODELETE)
    const std::string cmd = std::string("readelf -dW --dyn-syms '") + path + "' | grep -E 'UNIQUE|NODELETE|FLAGS'";
    std::fflush(stdout);
    (void)std::system(cmd.c_str());
#endif
  }
  return ok;
}

} // namespace

int main(int argc, char* argv[])
{
  if (argc != 2) {
    std::printf("usage: %s MODULE\n", argv[0]);
    return 2;
  }
  bool ok = true;
  for (int round = 1; round <= 2; ++round) {
    ok = loadRunUnload(argv[1], round) && ok;
  }
  std::printf(ok ? "pocketfft_unload_test: passed\n" : "pocketfft_unload_test: FAILED\n");
  return ok ? 0 : 1;
}
