// The module of pocketfft_unload_test (see pocketfft_unload_test.cpp): it includes the build's
// patched pocketfft_hdronly.h (patches/pocketfft-no-thread-local.sh), as GMIC.ofx does through
// CImg.h, and runs its thread pool. Built as GMIC.ofx is (a MODULE with hidden visibility), with
// one exported function; also built with the unpatched header, as the test's control.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pocketfft_hdronly.h"

namespace {

struct Result {
  bool ok = true;
  std::string log;
  void fail(const std::string& what)
  {
    ok = false;
    log += "FAILED: " + what + "\n";
  }
};

// FNV-1a of the bytes of v
uint64_t hash(const std::vector<std::complex<double>>& v)
{
  uint64_t h = 1469598103934665603ULL;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(v.data());
  for (size_t i = 0; i < v.size() * sizeof(v[0]); ++i) {
    h = (h ^ p[i]) * 1099511628211ULL;
  }
  return h;
}

// An in-place c2c of a w x h x d image along axis, as CImg's FFT() calls it: with 8 threads, the
// result must be bit-identical to the single-threaded one (each task transforms its own lines,
// given by the share that thread_map() passes it; with wrong shares, lines would be transformed
// twice or not at all), and the inverse must give
// the input back. Its hash goes into the log, to compare builds (before and after the patch).
void checkFFT(Result& r, size_t w, size_t h, size_t d, size_t axis)
{
  const size_t n = w * h * d;
  std::vector<std::complex<double>> input(n);
  for (size_t i = 0; i < n; ++i) {
    input[i] = std::complex<double>(std::sin(0.001 * double(i)) + double(i % 7), std::cos(0.003 * double(i)));
  }
  const ptrdiff_t s = sizeof(std::complex<double>);
  const pocketfft::shape_t shape{w, h, d};
  const pocketfft::stride_t stride{s, s * ptrdiff_t(w), s * ptrdiff_t(w * h)};
  const pocketfft::shape_t axes{axis};
  std::vector<std::complex<double>> one = input, many = input;
  pocketfft::c2c(shape, stride, stride, axes, true, one.data(), one.data(), 1., 1);
  pocketfft::c2c(shape, stride, stride, axes, true, many.data(), many.data(), 1., 8);
  char name[64];
  std::snprintf(name, sizeof(name), "%zux%zux%zu axis %zu", w, h, d, axis);
  if (std::memcmp(one.data(), many.data(), n * sizeof(one[0])) != 0) {
    r.fail(std::string(name) + ": the FFT with 8 threads differs from the single-threaded one");
  }
  std::vector<std::complex<double>> back = many;
  pocketfft::c2c(shape, stride, stride, axes, false, back.data(), back.data(), 1. / double(shape[axis]), 8);
  double err = 0;
  for (size_t i = 0; i < n; ++i) {
    err = std::max(err, std::abs(back[i] - input[i]));
  }
  if (!(err < 1e-9)) {
    r.fail(std::string(name) + ": the inverse FFT doesn't give the input back (error " + std::to_string(err) + ")");
  }
  char line[128];
  std::snprintf(line, sizeof(line), "%s: hash %016llx\n", name, (unsigned long long)hash(many));
  r.log += line;
}

} // namespace

// Runs the checks; writes their log into msg (truncated to len) and returns 0 if they passed.
extern "C" __attribute__((visibility("default"))) int pocketfft_unload_test_run(char* msg, size_t len)
{
  Result r;
  try {
    // (CImg's shapes: 3 axes; lines shorter than 1000 have 4 times fewer tasks)
    checkFFT(r, 512, 512, 1, 0);
    checkFFT(r, 512, 512, 1, 1);
    checkFFT(r, 720, 576, 3, 0);
    checkFFT(r, 720, 576, 3, 1);
    checkFFT(r, 720, 576, 3, 2);
    checkFFT(r, 1000, 300, 1, 0);
  } catch (const std::exception& e) {
    r.fail(std::string("exception: ") + e.what());
  }
  std::snprintf(msg, len, "%s", r.log.c_str());
  return r.ok ? 0 : 1;
}
