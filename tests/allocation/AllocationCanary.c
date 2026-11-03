/* A separately loaded module for tests/allocation/AllocationProbeTests.cpp:
 * on Windows it is built with the static CRT (/MT), like the instrument
 * DLLs, so the probe must see allocations made by another module's own CRT
 * and its own imports. */
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#define CANARY_EXPORT __declspec(dllexport)
#else
#define CANARY_EXPORT __attribute__((visibility("default")))
#endif

CANARY_EXPORT void *voxCanaryMalloc(size_t n) { return malloc(n); }
CANARY_EXPORT void *voxCanaryRealloc(void *p, size_t n) { return realloc(p, n); }
CANARY_EXPORT void voxCanaryFree(void *p) { free(p); }
#if defined(_WIN32)
CANARY_EXPORT void *voxCanaryHeapAlloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n); }
CANARY_EXPORT void voxCanaryHeapFree(void *p) { HeapFree(GetProcessHeap(), 0, p); }
#endif
