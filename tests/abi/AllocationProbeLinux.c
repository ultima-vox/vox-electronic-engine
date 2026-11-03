/* glibc allocation interposer. The test executable is linked with -rdynamic,
 * so a dynamically loaded module resolves these symbols here too. Every
 * public glibc allocation entry point is covered: malloc, calloc, realloc,
 * reallocarray, posix_memalign, aligned_alloc, memalign, valloc and pvalloc
 * (C++ operator new, aligned new and the standard containers all reach one
 * of them). Only the thread that called voxProbeBegin is counted, into its
 * own count (static TLS of the executable, no allocation on access), so
 * background threads, the
 * sample loader worker among them, cannot make the probe flaky or hide an
 * allocation on the probed thread. Not built on other platforms; see
 * tests/support/AllocationProbeWindows.c. */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);
extern void *__libc_valloc(size_t);
extern void *__libc_pvalloc(size_t);

/* Both per thread: each probing thread gets its own count. */
static _Thread_local int probing;
static _Thread_local unsigned long allocations;

static inline void counted(void) {
  if (probing)
    ++allocations;
}

void *malloc(size_t size) {
  counted();
  return __libc_malloc(size);
}

void *calloc(size_t count, size_t size) {
  counted();
  return __libc_calloc(count, size);
}

void *realloc(void *pointer, size_t size) {
  counted();
  return __libc_realloc(pointer, size);
}

void *reallocarray(void *pointer, size_t count, size_t size) {
  counted();
  if (size != 0 && count > SIZE_MAX / size) {
    errno = ENOMEM;
    return NULL;
  }
  return __libc_realloc(pointer, count * size);
}

void *memalign(size_t alignment, size_t size) {
  counted();
  return __libc_memalign(alignment, size);
}

void *aligned_alloc(size_t alignment, size_t size) {
  counted();
  return __libc_memalign(alignment, size);
}

int posix_memalign(void **out, size_t alignment, size_t size) {
  counted();
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
    return EINVAL;
  void *p = __libc_memalign(alignment, size);
  if (p == NULL && size != 0)
    return ENOMEM;
  *out = p;
  return 0;
}

void *valloc(size_t size) {
  counted();
  return __libc_valloc(size);
}

void *pvalloc(size_t size) {
  counted();
  return __libc_pvalloc(size);
}

int voxProbeAvailable(void) { return 1; }

void voxProbeBegin(void) {
  allocations = 0;
  probing = 1;
}

unsigned long voxProbeEnd(void) {
  probing = 0;
  return allocations;
}
