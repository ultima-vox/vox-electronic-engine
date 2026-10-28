/* glibc-only allocation interposer. The test executable is linked with
 * -rdynamic, so the dynamically loaded module resolves malloc/calloc/realloc
 * here too; counting covers host and module code on the probing thread's
 * window. Not built on other platforms. */
#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);

static atomic_int probing;
static atomic_ulong allocations;

void *malloc(size_t size) {
  if (atomic_load_explicit(&probing, memory_order_relaxed))
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_malloc(size);
}

void *calloc(size_t count, size_t size) {
  if (atomic_load_explicit(&probing, memory_order_relaxed))
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_calloc(count, size);
}

void *realloc(void *pointer, size_t size) {
  if (atomic_load_explicit(&probing, memory_order_relaxed))
    atomic_fetch_add_explicit(&allocations, 1, memory_order_relaxed);
  return __libc_realloc(pointer, size);
}

void voxProbeBegin(void) {
  atomic_store(&allocations, 0);
  atomic_store(&probing, 1);
}

unsigned long voxProbeEnd(void) {
  atomic_store(&probing, 0);
  return atomic_load(&allocations);
}
