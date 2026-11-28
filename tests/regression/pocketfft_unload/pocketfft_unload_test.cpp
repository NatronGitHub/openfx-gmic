/*
  pocketfft_unload_test MODULE: regression test of patches/pocketfft-no-thread-local.sh
  (https://github.com/NatronGitHub/openfx/issues/385).

  pocketfft's thread pool had two thread-local variables (threading::thread_id() and
  num_threads()), and macOS (dyld) never unloads an image that has any (MH_HAS_TLV_DESCRIPTORS):
  GMIC.ofx, which uses the pool through CImg's FFT, could not be unloaded. The patch keeps each
  task's share of the work in a pthread key instead, and drops the pthread_atfork() handlers.

  MODULE (pocketfft_fft_module.cpp) includes the build's patched pocketfft_hdronly.h. This
  program loads it, runs its checks (each thread_map() task's share, multithreaded FFTs
  bit-identical to single-threaded ones, round trips), closes it, and checks that it is gone
  (dlopen() with RTLD_NOLOAD doesn't find it), which also runs the pool's destructor (joining its
  worker threads) and the key's deletion inside dlclose(). On macOS, the module must also have no
  thread-local variables. Twice: the second load creates a new pool and key. Exits with 0 if all
  passed. Not on Windows, where pocketfft is single-threaded in GMIC.ofx (CMakeLists.txt).
*/

#include <cstdio>

#include <dlfcn.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#endif

namespace {

typedef int (*Run)(char*, size_t);

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
#endif
  if (dlclose(handle) != 0) {
    std::printf("FAILED: round %d: dlclose: %s\n", round, dlerror());
    return false;
  }
  if (void* again = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD)) {
    std::printf("FAILED: round %d: %s is still loaded after dlclose()\n", round, path);
    dlclose(again);
    ok = false;
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
