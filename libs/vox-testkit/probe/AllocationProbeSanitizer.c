/* Allocation probe for sanitizer builds (ThreadSanitizer, see
 * VOX_SANITIZE_THREAD). TSan intercepts malloc itself, so the glibc
 * interposer (tests/abi/AllocationProbeLinux.c) cannot be linked; instead the
 * sanitizer allocator calls a malloc hook for every user allocation (malloc,
 * calloc, realloc, the aligned variants, operator new). As in the other
 * probes, only the thread that called voxProbeBegin counts, into its own
 * count. */
#include <stddef.h>

int __sanitizer_install_malloc_and_free_hooks(void (*malloc_hook)(const volatile void *, size_t),
                                              void (*free_hook)(const volatile void *));

static _Thread_local int probing;
static _Thread_local unsigned long allocations;
static int installed;

static void onMalloc(const volatile void *pointer, size_t size) {
  (void)pointer;
  (void)size;
  if (probing)
    ++allocations;
}

static void onFree(const volatile void *pointer) { (void)pointer; }

int voxProbeAvailable(void) { return 1; }

void voxProbeBegin(void) {
  if (!__atomic_load_n(&installed, __ATOMIC_ACQUIRE) &&
      __sanitizer_install_malloc_and_free_hooks(onMalloc, onFree) != 0)
    __atomic_store_n(&installed, 1, __ATOMIC_RELEASE);
  allocations = 0;
  probing = 1;
}

unsigned long voxProbeEnd(void) {
  probing = 0;
  return allocations;
}
