// Proves the realtime allocation probe (tests/support/AllocationProbe.h) on
// the machine that runs it: every allocation path an audio callback could
// take is counted on the probed thread (executable, standard library and a
// separately loaded module with its own CRT), and other threads are not.
// A probe that silently misses a path would make every 0-allocation result
// meaningless, so this test runs wherever the probe is built.
#include "AllocationProbe.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <malloc.h>
#include <objbase.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <malloc.h>
#endif

namespace {
int checks = 0;
void check(bool ok, const std::string &message) {
  ++checks;
  if (!ok) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

// Optimizers may elide a malloc/free pair; route pointers through here.
// Per thread: threadsAreSeparate() calls keep() from several threads, and a
// shared sink would be a data race (review 5755905326).
thread_local void *volatile sink;
void keep(void *p) { sink = p; }

template <class F> unsigned long probed(F &&f) {
  voxProbeBegin();
  f();
  return voxProbeEnd();
}

void counts(const char *what, unsigned long n) {
  check(n >= 1, std::string(what) + " is counted (" + std::to_string(n) + ")");
}

struct alignas(64) Wide {
  float lane[16];
};

void standardPaths() {
  void *p = nullptr;
  counts("malloc", probed([&] { keep(p = std::malloc(64)); }));
  std::free(p);
  counts("calloc", probed([&] { keep(p = std::calloc(4, 16)); }));
  counts("realloc", probed([&] { keep(p = std::realloc(p, 4096)); }));
  std::free(p);
  int *i = nullptr;
  counts("operator new", probed([&] { keep(i = new int(7)); }));
  delete i;
  counts("operator new[]", probed([&] { keep(i = new int[100]); }));
  delete[] i;
  Wide *w = nullptr;
  counts("aligned operator new", probed([&] { keep(w = new Wide()); }));
  delete w;
  counts("nothrow new", probed([&] { keep(i = new (std::nothrow) int(1)); }));
  delete i;
  std::vector<float> v;
  counts("std::vector growth", probed([&] { v.resize(1000); }));
  std::string s;
  counts("std::string beyond SSO", probed([&] { s.assign(200, 'x'); }));
  std::shared_ptr<int> sp;
  counts("std::make_shared", probed([&] { sp = std::make_shared<int>(3); }));
  std::function<int()> fn;
  counts("std::function with a large capture", probed([&] {
           std::array<char, 256> big{};
           fn = [big] { return static_cast<int>(big[0]); };
         }));
}

void platformPaths(const char *canaryPath) {
#if defined(_WIN32)
  void *p = nullptr;
  HANDLE heap = GetProcessHeap();
  counts("HeapAlloc", probed([&] { keep(p = HeapAlloc(heap, 0, 128)); }));
  counts("HeapReAlloc", probed([&] { keep(p = HeapReAlloc(heap, 0, p, 8192)); }));
  HeapFree(heap, 0, p);
  counts("VirtualAlloc", probed([&] {
           keep(p = VirtualAlloc(nullptr, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
         }));
  VirtualFree(p, 0, MEM_RELEASE);
  counts("LocalAlloc", probed([&] { keep(p = LocalAlloc(LMEM_FIXED, 64)); }));
  LocalFree(p);
  counts("GlobalAlloc", probed([&] { keep(p = GlobalAlloc(GMEM_FIXED, 64)); }));
  GlobalFree(p);
  counts("_aligned_malloc", probed([&] { keep(p = _aligned_malloc(256, 64)); }));
  _aligned_free(p);
  // combase must be loaded before the probe walks the modules.
  check(CoInitializeEx(nullptr, COINIT_MULTITHREADED) >= 0, "COM initialised");
  counts("CoTaskMemAlloc", probed([&] { keep(p = CoTaskMemAlloc(64)); }));
  CoTaskMemFree(p);
  CoUninitialize();

  HMODULE canary = LoadLibraryA(canaryPath);
  check(canary != nullptr, "the /MT canary DLL loads");
  using Alloc = void *(*)(size_t);
  using Realloc = void *(*)(void *, size_t);
  using Free = void (*)(void *);
  auto cm = reinterpret_cast<Alloc>(GetProcAddress(canary, "voxCanaryMalloc"));
  auto cr = reinterpret_cast<Realloc>(GetProcAddress(canary, "voxCanaryRealloc"));
  auto cf = reinterpret_cast<Free>(GetProcAddress(canary, "voxCanaryFree"));
  auto ch = reinterpret_cast<Alloc>(GetProcAddress(canary, "voxCanaryHeapAlloc"));
  auto chf = reinterpret_cast<Free>(GetProcAddress(canary, "voxCanaryHeapFree"));
  check(cm && cr && cf && ch && chf, "canary exports");
#else
  void *p = nullptr;
#if defined(VOX_PROBE_SANITIZER)
  // The sanitizer allocator runs no malloc hook for these; the normal Linux
  // build counts them (AllocationProbeLinux.c).
  std::cout << "sanitizer probe: posix_memalign, aligned_alloc, memalign and valloc "
               "are not hooked by the sanitizer; covered in the normal build\n";
#else
  counts("posix_memalign", probed([&] { check(posix_memalign(&p, 64, 256) == 0, "pm"); }));
  std::free(p);
  counts("aligned_alloc", probed([&] { keep(p = aligned_alloc(64, 256)); }));
  std::free(p);
  counts("memalign", probed([&] { keep(p = memalign(64, 256)); }));
  std::free(p);
  counts("valloc", probed([&] { keep(p = valloc(256)); }));
  std::free(p);
#endif
  counts("reallocarray", probed([&] { keep(p = reallocarray(nullptr, 8, 32)); }));
  std::free(p);

  void *canary = dlopen(canaryPath, RTLD_NOW | RTLD_LOCAL);
  check(canary != nullptr, "the canary module loads");
  using Alloc = void *(*)(size_t);
  using Realloc = void *(*)(void *, size_t);
  using Free = void (*)(void *);
  auto cm = reinterpret_cast<Alloc>(dlsym(canary, "voxCanaryMalloc"));
  auto cr = reinterpret_cast<Realloc>(dlsym(canary, "voxCanaryRealloc"));
  auto cf = reinterpret_cast<Free>(dlsym(canary, "voxCanaryFree"));
  check(cm && cr && cf, "canary exports");
#endif
  // The module was loaded after the earlier probes: voxProbeBegin must hook
  // it too (Windows re-walks the modules; Linux resolves to the executable).
  void *q = nullptr;
  counts("malloc inside a separately loaded module", probed([&] { keep(q = cm(64)); }));
  counts("realloc inside a separately loaded module", probed([&] { keep(q = cr(q, 9000)); }));
  cf(q);
#if defined(_WIN32)
  counts("HeapAlloc inside a separately loaded /MT module", probed([&] { keep(q = ch(64)); }));
  chf(q);
#endif
}

void quietCodeCountsZero() {
  float acc = 0.0f;
  std::array<float, 512> buffer{};
  const auto n = probed([&] {
    for (int k = 0; k < 1000; ++k)
      for (auto &x : buffer)
        acc += (x = x * 0.5f + 1.0f);
  });
  check(n == 0, "arithmetic on preallocated memory counts 0 (" + std::to_string(n) + ")");
  keep(&acc);
}

// The probe is per thread: a background thread allocating all the time
// (as the sample loader does while it decodes) never shows up in the probed
// thread's count, and a background thread that probes itself gets its own.
void threadsAreSeparate() {
  std::atomic<bool> run{true}, started{false};
  std::atomic<unsigned long> background{0};
  std::thread noisy([&] {
    started = true;
    while (run) {
      void *p = std::malloc(256);
      keep(p);
      std::free(p);
      std::vector<int> v(64);
      keep(v.data());
    }
  });
  std::thread selfProbing([&] {
    while (!started) {
    }
    voxProbeBegin();
    for (int k = 0; k < 100; ++k) {
      void *p = std::malloc(32);
      keep(p);
      std::free(p);
    }
    background = voxProbeEnd();
  });
  while (!started) {
  }
  float acc = 0.0f;
  const auto quiet = probed([&] {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    while (std::chrono::steady_clock::now() < until)
      acc += 1.0f;
  });
  selfProbing.join();
  run = false;
  noisy.join();
  keep(&acc);
  check(quiet == 0, "another thread's allocations are not counted on the probed thread (" +
                        std::to_string(quiet) + ")");
  check(background >= 100, "a probing background thread counts its own allocations (" +
                               std::to_string(background.load()) + ")");
}
} // namespace

int main(int argc, char **argv) {
  check(argc == 2, "usage: allocation_probe_tests <canary module>");
  check(voxProbeAvailable() == 1, "the allocation probe is built in");
  quietCodeCountsZero();
  standardPaths();
  platformPaths(argv[1]);
  threadsAreSeparate();
  std::cout << "allocation_probe_tests: " << checks << " checks passed\n";
  return 0;
}
