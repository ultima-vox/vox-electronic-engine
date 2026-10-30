/* glibc-only allocation interposer. The test executable is linked with
 * -rdynamic, so the dynamically loaded module resolves malloc/calloc/realloc
 * here too. Only the thread that called voxProbeBegin is counted (static TLS
 * of the executable, no allocation on access), so unrelated background
 * threads cannot make the probe flaky. Not built on other platforms. */
#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);

static _Thread_local int probing;
static atomic_ulong allocations;

void *malloc(size_t size) {
  if (probing)
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_malloc(size);
}

void *calloc(size_t count, size_t size) {
  if (probing)
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_calloc(count, size);
}

void *realloc(void *pointer, size_t size) {
  if (probing)
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_realloc(pointer, size);
}

void voxProbeBegin(void) {
  atomic_store(&allocations, 0);
  probing = 1;
}

unsigned long voxProbeEnd(void) {
  probing = 0;
  return atomic_load(&allocations);
}
