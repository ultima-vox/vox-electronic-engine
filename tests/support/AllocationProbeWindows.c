/* Windows realtime allocation probe (see AllocationProbe.h).
 *
 * Mechanism: import address table hooks. voxProbeBegin() walks every module
 * loaded in the process (the test executable, the Rack, each instrument DLL
 * with its static CRT, ucrtbase, kernelbase, combase, ...) and redirects
 * each import slot that points at an allocation entry point to a counting
 * wrapper. Slots are matched by address, so forwarded exports
 * (kernel32!HeapAlloc -> ntdll!RtlAllocateHeap) and API-set imports
 * (api-ms-win-crt-heap-l1-1-0 -> ucrtbase) are covered whatever name the
 * importer used.
 *
 * Covered entry points:
 *   ntdll      RtlAllocateHeap, RtlReAllocateHeap, NtAllocateVirtualMemory,
 *              NtAllocateVirtualMemoryEx
 *   kernelbase VirtualAlloc, VirtualAllocEx, VirtualAllocExNuma, VirtualAlloc2,
 *              LocalAlloc, LocalReAlloc, GlobalAlloc, GlobalReAlloc
 *   kernel32   the same names where kernel32 has its own stub
 *   ucrtbase   malloc, calloc, realloc, _recalloc, _expand, _malloc_base,
 *              _calloc_base, _realloc_base, _aligned_malloc,
 *              _aligned_realloc, _aligned_recalloc, _aligned_offset_malloc
 *   combase    CoTaskMemAlloc, CoTaskMemRealloc (and ole32 where not forwarded)
 * A static CRT (/MT, the instrument DLLs) reaches the OS heap through its
 * own import of HeapAlloc/HeapReAlloc, which resolves to the ntdll heap
 * functions above; operator new, aligned new and the standard containers
 * reach malloc or _aligned_malloc. tests/allocation/AllocationProbeTests.cpp
 * proves each path on this machine (canaries in the executable and in a
 * separately loaded /MT DLL).
 *
 * Counting is per thread (static TLS of the executable, no allocation on
 * access): only the thread that called voxProbeBegin() counts, into its own
 * count, so loader and collector threads never mix in.
 *
 * Not covered (documented limits): allocations made by ntdll internally
 * without going through an import slot, delay-load imports not yet bound,
 * and modules loaded after voxProbeBegin() (call it again after loading). */
#define WIN32_LEAN_AND_MEAN
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2 /* K32EnumProcessModules from kernel32, no psapi.lib */
#endif
#include <windows.h>
#include <psapi.h>
#include <stddef.h>

typedef LONG NTSTATUS_T;

static __declspec(thread) int probing;
static __declspec(thread) unsigned long allocations;

#define COUNTED()                                                                          \
  do {                                                                                     \
    if (probing)                                                                           \
      ++allocations;                                                                       \
  } while (0)

/* One hook per entry point: an original pointer and a counting wrapper with
 * the entry point's exact signature. */
#define HOOK(tag, ret, conv, params, args)                                                 \
  typedef ret(conv *tag##_fn) params;                                                      \
  static tag##_fn tag##_original;                                                          \
  static ret conv tag##_hook params {                                                      \
    COUNTED();                                                                             \
    return tag##_original args;                                                            \
  }

/* ntdll */
HOOK(rtlAlloc, PVOID, NTAPI, (PVOID h, ULONG f, SIZE_T n), (h, f, n))
HOOK(rtlReAlloc, PVOID, NTAPI, (PVOID h, ULONG f, PVOID p, SIZE_T n), (h, f, p, n))
HOOK(ntAllocVm, NTSTATUS_T, NTAPI,
     (HANDLE pr, PVOID *base, ULONG_PTR z, PSIZE_T n, ULONG t, ULONG prot),
     (pr, base, z, n, t, prot))
HOOK(ntAllocVmEx, NTSTATUS_T, NTAPI,
     (HANDLE pr, PVOID *base, PSIZE_T n, ULONG t, ULONG prot, PVOID ext, ULONG count),
     (pr, base, n, t, prot, ext, count))
/* kernelbase and kernel32 (separate originals: kernel32 may have stubs) */
#define VM_HOOKS(m)                                                                        \
  HOOK(m##VirtualAlloc, LPVOID, WINAPI, (LPVOID a, SIZE_T n, DWORD t, DWORD p),             \
       (a, n, t, p))                                                                       \
  HOOK(m##VirtualAllocEx, LPVOID, WINAPI,                                                  \
       (HANDLE pr, LPVOID a, SIZE_T n, DWORD t, DWORD p), (pr, a, n, t, p))                 \
  HOOK(m##VirtualAllocExNuma, LPVOID, WINAPI,                                              \
       (HANDLE pr, LPVOID a, SIZE_T n, DWORD t, DWORD p, DWORD node),                      \
       (pr, a, n, t, p, node))                                                             \
  HOOK(m##VirtualAlloc2, PVOID, WINAPI,                                                    \
       (HANDLE pr, PVOID a, SIZE_T n, ULONG t, ULONG p, PVOID ext, ULONG count),           \
       (pr, a, n, t, p, ext, count))                                                       \
  HOOK(m##LocalAlloc, HLOCAL, WINAPI, (UINT f, SIZE_T n), (f, n))                           \
  HOOK(m##LocalReAlloc, HLOCAL, WINAPI, (HLOCAL h, SIZE_T n, UINT f), (h, n, f))            \
  HOOK(m##GlobalAlloc, HGLOBAL, WINAPI, (UINT f, SIZE_T n), (f, n))                         \
  HOOK(m##GlobalReAlloc, HGLOBAL, WINAPI, (HGLOBAL h, SIZE_T n, UINT f), (h, n, f))
VM_HOOKS(kb_)
VM_HOOKS(k32_)
/* ucrtbase */
HOOK(crtMalloc, void *, __cdecl, (size_t n), (n))
HOOK(crtCalloc, void *, __cdecl, (size_t c, size_t n), (c, n))
HOOK(crtRealloc, void *, __cdecl, (void *p, size_t n), (p, n))
HOOK(crtRecalloc, void *, __cdecl, (void *p, size_t c, size_t n), (p, c, n))
HOOK(crtExpand, void *, __cdecl, (void *p, size_t n), (p, n))
HOOK(crtMallocBase, void *, __cdecl, (size_t n), (n))
HOOK(crtCallocBase, void *, __cdecl, (size_t c, size_t n), (c, n))
HOOK(crtReallocBase, void *, __cdecl, (void *p, size_t n), (p, n))
HOOK(crtAlignedMalloc, void *, __cdecl, (size_t n, size_t a), (n, a))
HOOK(crtAlignedRealloc, void *, __cdecl, (void *p, size_t n, size_t a), (p, n, a))
HOOK(crtAlignedRecalloc, void *, __cdecl, (void *p, size_t c, size_t n, size_t a),
     (p, c, n, a))
HOOK(crtAlignedOffsetMalloc, void *, __cdecl, (size_t n, size_t a, size_t o), (n, a, o))
/* combase, and ole32 where it has its own stubs */
HOOK(coAlloc, LPVOID, WINAPI, (SIZE_T n), (n))
HOOK(coRealloc, LPVOID, WINAPI, (LPVOID p, SIZE_T n), (p, n))
HOOK(oleAlloc, LPVOID, WINAPI, (SIZE_T n), (n))
HOOK(oleRealloc, LPVOID, WINAPI, (LPVOID p, SIZE_T n), (p, n))

typedef struct {
  const char *module;
  const char *name;
  void **original; /* resolved export address, called by the hook */
  void *hook;
} Target;

#define T(module, name, tag) {module, name, (void **)&tag##_original, (void *)tag##_hook}
#define VM_TARGETS(module, m)                                                              \
  T(module, "VirtualAlloc", m##VirtualAlloc), T(module, "VirtualAllocEx", m##VirtualAllocEx), \
      T(module, "VirtualAllocExNuma", m##VirtualAllocExNuma),                              \
      T(module, "VirtualAlloc2", m##VirtualAlloc2), T(module, "LocalAlloc", m##LocalAlloc), \
      T(module, "LocalReAlloc", m##LocalReAlloc), T(module, "GlobalAlloc", m##GlobalAlloc), \
      T(module, "GlobalReAlloc", m##GlobalReAlloc)

static Target targets[] = {
    T("ntdll.dll", "RtlAllocateHeap", rtlAlloc),
    T("ntdll.dll", "RtlReAllocateHeap", rtlReAlloc),
    T("ntdll.dll", "NtAllocateVirtualMemory", ntAllocVm),
    T("ntdll.dll", "NtAllocateVirtualMemoryEx", ntAllocVmEx),
    VM_TARGETS("kernelbase.dll", kb_),
    VM_TARGETS("kernel32.dll", k32_),
    T("ucrtbase.dll", "malloc", crtMalloc),
    T("ucrtbase.dll", "calloc", crtCalloc),
    T("ucrtbase.dll", "realloc", crtRealloc),
    T("ucrtbase.dll", "_recalloc", crtRecalloc),
    T("ucrtbase.dll", "_expand", crtExpand),
    T("ucrtbase.dll", "_malloc_base", crtMallocBase),
    T("ucrtbase.dll", "_calloc_base", crtCallocBase),
    T("ucrtbase.dll", "_realloc_base", crtReallocBase),
    T("ucrtbase.dll", "_aligned_malloc", crtAlignedMalloc),
    T("ucrtbase.dll", "_aligned_realloc", crtAlignedRealloc),
    T("ucrtbase.dll", "_aligned_recalloc", crtAlignedRecalloc),
    T("ucrtbase.dll", "_aligned_offset_malloc", crtAlignedOffsetMalloc),
    T("combase.dll", "CoTaskMemAlloc", coAlloc),
    T("combase.dll", "CoTaskMemRealloc", coRealloc),
    T("ole32.dll", "CoTaskMemAlloc", oleAlloc),
    T("ole32.dll", "CoTaskMemRealloc", oleRealloc),
};
#define TARGET_COUNT (sizeof(targets) / sizeof(targets[0]))

/* Resolves the targets whose module is loaded. A target whose export
 * resolves to the same address as an earlier one (a forwarder) is left
 * unresolved: the earlier hook already covers that address. */
static void resolve(void) {
  for (size_t i = 0; i < TARGET_COUNT; ++i) {
    if (*targets[i].original != NULL)
      continue;
    HMODULE module = GetModuleHandleA(targets[i].module);
    if (module == NULL)
      continue;
    void *address = (void *)GetProcAddress(module, targets[i].name);
    if (address == NULL)
      continue;
    int duplicate = 0;
    for (size_t j = 0; j < TARGET_COUNT; ++j)
      if (j != i && *targets[j].original == address)
        duplicate = 1;
    if (!duplicate)
      *targets[i].original = address;
  }
}

static void patchSlot(void **slot) {
  void *current = *slot;
  for (size_t i = 0; i < TARGET_COUNT; ++i) {
    if (*targets[i].original == NULL || current != *targets[i].original)
      continue;
    DWORD old = 0;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
      InterlockedExchangePointer(slot, targets[i].hook);
      VirtualProtect(slot, sizeof(void *), old, &old);
      FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    }
    return;
  }
}

static void patchModule(HMODULE module) {
  BYTE *base = (BYTE *)module;
  const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return;
  const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return;
  const IMAGE_DATA_DIRECTORY *dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (dir->VirtualAddress == 0 || dir->Size == 0)
    return;
  for (const IMAGE_IMPORT_DESCRIPTOR *d =
           (const IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);
       d->Name != 0; ++d) {
    if (d->FirstThunk == 0)
      continue;
    for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
         t->u1.Function != 0; ++t)
      patchSlot((void **)&t->u1.Function);
  }
}

static void install(void) {
  resolve();
  HMODULE modules[1024];
  DWORD needed = 0;
  if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
    return;
  DWORD count = needed / sizeof(HMODULE);
  if (count > sizeof(modules) / sizeof(modules[0]))
    count = sizeof(modules) / sizeof(modules[0]);
  for (DWORD i = 0; i < count; ++i)
    patchModule(modules[i]);
}

int voxProbeAvailable(void) { return 1; }

/* Re-installs on every call so modules loaded since the last one (a .vins
 * instrument DLL) are hooked too. The walk itself runs before counting
 * starts. */
void voxProbeBegin(void) {
  probing = 0;
  install();
  allocations = 0;
  probing = 1;
}

unsigned long voxProbeEnd(void) {
  probing = 0;
  return allocations;
}
