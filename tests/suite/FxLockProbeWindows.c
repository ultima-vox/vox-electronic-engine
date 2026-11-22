/*
 * Windows synchronization probe for the FX callback fixture.
 *
 * This is deliberately test-only. It uses import-address-table hooks so the
 * measured thread can count the Win32 synchronization entry points used by
 * the executable, JUCE and the loaded FX module without adding an interposer
 * to the product. voxFxLockPrepare() walks modules while counting is disabled;
 * voxFxLockBegin() only arms the thread-local counter, so allocator-probe
 * setup cannot be mistaken for an audio lock. The measured processBlock is
 * the only region with the counter enabled.
 *
 * Covered lock entries:
 *   EnterCriticalSection, TryEnterCriticalSection,
 *   AcquireSRWLock{Exclusive,Shared}, TryAcquireSRWLock{Exclusive,Shared}
 * Covered wait entries:
 *   WaitFor{Single,Multiple}Object{,Ex}, SignalObjectAndWait,
 *   WaitOnAddress, SleepConditionVariable{CS,SRW}
 *
 * The probe does not see locks implemented entirely inside an unhooked module,
 * ntdll-internal waits, or modules loaded after the begin walk. A zero count
 * therefore means zero observed entries in this loaded fixture, not a formal
 * proof about a host or the whole operating system.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <windows.h>
#include <psapi.h>
#include <stddef.h>

static __declspec(thread) int vox_fx_lock_active;
static __declspec(thread) unsigned long vox_fx_lock_count;
static __declspec(thread) unsigned long vox_fx_wait_count;

#define VOX_FX_COUNT_LOCK() do { if (vox_fx_lock_active) ++vox_fx_lock_count; } while (0)
#define VOX_FX_COUNT_WAIT() do { if (vox_fx_lock_active) ++vox_fx_wait_count; } while (0)

/* Lock entry points. */
typedef VOID (WINAPI *EnterCriticalSectionFn)(LPCRITICAL_SECTION);
static EnterCriticalSectionFn enterCriticalSectionOriginal;
static VOID WINAPI enterCriticalSectionHook(LPCRITICAL_SECTION lock) {
  VOX_FX_COUNT_LOCK();
  enterCriticalSectionOriginal(lock);
}

typedef BOOL (WINAPI *TryEnterCriticalSectionFn)(LPCRITICAL_SECTION);
static TryEnterCriticalSectionFn tryEnterCriticalSectionOriginal;
static BOOL WINAPI tryEnterCriticalSectionHook(LPCRITICAL_SECTION lock) {
  VOX_FX_COUNT_LOCK();
  return tryEnterCriticalSectionOriginal(lock);
}

typedef VOID (WINAPI *AcquireSrwLockFn)(PSRWLOCK);
static AcquireSrwLockFn acquireSrwLockExclusiveOriginal;
static VOID WINAPI acquireSrwLockExclusiveHook(PSRWLOCK lock) {
  VOX_FX_COUNT_LOCK();
  acquireSrwLockExclusiveOriginal(lock);
}
static AcquireSrwLockFn acquireSrwLockSharedOriginal;
static VOID WINAPI acquireSrwLockSharedHook(PSRWLOCK lock) {
  VOX_FX_COUNT_LOCK();
  acquireSrwLockSharedOriginal(lock);
}

typedef BOOLEAN (WINAPI *TryAcquireSrwLockFn)(PSRWLOCK);
static TryAcquireSrwLockFn tryAcquireSrwLockExclusiveOriginal;
static BOOLEAN WINAPI tryAcquireSrwLockExclusiveHook(PSRWLOCK lock) {
  VOX_FX_COUNT_LOCK();
  return tryAcquireSrwLockExclusiveOriginal(lock);
}
static TryAcquireSrwLockFn tryAcquireSrwLockSharedOriginal;
static BOOLEAN WINAPI tryAcquireSrwLockSharedHook(PSRWLOCK lock) {
  VOX_FX_COUNT_LOCK();
  return tryAcquireSrwLockSharedOriginal(lock);
}

/* Wait entry points. */
typedef DWORD (WINAPI *WaitForSingleObjectFn)(HANDLE, DWORD);
static WaitForSingleObjectFn waitForSingleObjectOriginal;
static DWORD WINAPI waitForSingleObjectHook(HANDLE handle, DWORD timeout) {
  VOX_FX_COUNT_WAIT();
  return waitForSingleObjectOriginal(handle, timeout);
}

typedef DWORD (WINAPI *WaitForSingleObjectExFn)(HANDLE, DWORD, BOOL);
static WaitForSingleObjectExFn waitForSingleObjectExOriginal;
static DWORD WINAPI waitForSingleObjectExHook(HANDLE handle, DWORD timeout, BOOL alertable) {
  VOX_FX_COUNT_WAIT();
  return waitForSingleObjectExOriginal(handle, timeout, alertable);
}

typedef DWORD (WINAPI *WaitForMultipleObjectsFn)(DWORD, const HANDLE *, BOOL, DWORD);
static WaitForMultipleObjectsFn waitForMultipleObjectsOriginal;
static DWORD WINAPI waitForMultipleObjectsHook(DWORD count, const HANDLE *handles,
                                                BOOL waitAll, DWORD timeout) {
  VOX_FX_COUNT_WAIT();
  return waitForMultipleObjectsOriginal(count, handles, waitAll, timeout);
}

typedef DWORD (WINAPI *WaitForMultipleObjectsExFn)(DWORD, const HANDLE *, BOOL, DWORD, BOOL);
static WaitForMultipleObjectsExFn waitForMultipleObjectsExOriginal;
static DWORD WINAPI waitForMultipleObjectsExHook(DWORD count, const HANDLE *handles,
                                                  BOOL waitAll, DWORD timeout, BOOL alertable) {
  VOX_FX_COUNT_WAIT();
  return waitForMultipleObjectsExOriginal(count, handles, waitAll, timeout, alertable);
}

typedef DWORD (WINAPI *SignalObjectAndWaitFn)(HANDLE, HANDLE, DWORD, BOOL);
static SignalObjectAndWaitFn signalObjectAndWaitOriginal;
static DWORD WINAPI signalObjectAndWaitHook(HANDLE signal, HANDLE wait,
                                             DWORD timeout, BOOL alertable) {
  VOX_FX_COUNT_WAIT();
  return signalObjectAndWaitOriginal(signal, wait, timeout, alertable);
}

typedef BOOL (WINAPI *WaitOnAddressFn)(volatile VOID *, PVOID, SIZE_T, DWORD);
static WaitOnAddressFn waitOnAddressOriginal;
static BOOL WINAPI waitOnAddressHook(volatile VOID *address, PVOID compare,
                                      SIZE_T size, DWORD timeout) {
  VOX_FX_COUNT_WAIT();
  return waitOnAddressOriginal(address, compare, size, timeout);
}

typedef BOOL (WINAPI *SleepConditionVariableCsFn)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);
static SleepConditionVariableCsFn sleepConditionVariableCsOriginal;
static BOOL WINAPI sleepConditionVariableCsHook(PCONDITION_VARIABLE condition,
                                                 PCRITICAL_SECTION lock, DWORD timeout) {
  VOX_FX_COUNT_WAIT();
  return sleepConditionVariableCsOriginal(condition, lock, timeout);
}

typedef BOOL (WINAPI *SleepConditionVariableSrwFn)(PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG);
static SleepConditionVariableSrwFn sleepConditionVariableSrwOriginal;
static BOOL WINAPI sleepConditionVariableSrwHook(PCONDITION_VARIABLE condition, PSRWLOCK lock,
                                                  DWORD timeout, ULONG flags) {
  VOX_FX_COUNT_WAIT();
  return sleepConditionVariableSrwOriginal(condition, lock, timeout, flags);
}

typedef struct {
  const char *module;
  const char *name;
  void **original;
  void *hook;
  int wait;
} VoxFxTarget;

#define LOCK_TARGET(module, name, original, hook) \
  { module, name, (void **)&original, (void *)hook, 0 }
#define WAIT_TARGET(module, name, original, hook) \
  { module, name, (void **)&original, (void *)hook, 1 }

static VoxFxTarget vox_fx_targets[] = {
  LOCK_TARGET("kernelbase.dll", "EnterCriticalSection",
              enterCriticalSectionOriginal, enterCriticalSectionHook),
  LOCK_TARGET("kernelbase.dll", "TryEnterCriticalSection",
              tryEnterCriticalSectionOriginal, tryEnterCriticalSectionHook),
  LOCK_TARGET("kernelbase.dll", "AcquireSRWLockExclusive",
              acquireSrwLockExclusiveOriginal, acquireSrwLockExclusiveHook),
  LOCK_TARGET("kernelbase.dll", "AcquireSRWLockShared",
              acquireSrwLockSharedOriginal, acquireSrwLockSharedHook),
  LOCK_TARGET("kernelbase.dll", "TryAcquireSRWLockExclusive",
              tryAcquireSrwLockExclusiveOriginal, tryAcquireSrwLockExclusiveHook),
  LOCK_TARGET("kernelbase.dll", "TryAcquireSRWLockShared",
              tryAcquireSrwLockSharedOriginal, tryAcquireSrwLockSharedHook),
  WAIT_TARGET("kernelbase.dll", "WaitForSingleObject",
              waitForSingleObjectOriginal, waitForSingleObjectHook),
  WAIT_TARGET("kernelbase.dll", "WaitForSingleObjectEx",
              waitForSingleObjectExOriginal, waitForSingleObjectExHook),
  WAIT_TARGET("kernelbase.dll", "WaitForMultipleObjects",
              waitForMultipleObjectsOriginal, waitForMultipleObjectsHook),
  WAIT_TARGET("kernelbase.dll", "WaitForMultipleObjectsEx",
              waitForMultipleObjectsExOriginal, waitForMultipleObjectsExHook),
  WAIT_TARGET("kernelbase.dll", "SignalObjectAndWait",
              signalObjectAndWaitOriginal, signalObjectAndWaitHook),
  WAIT_TARGET("kernelbase.dll", "WaitOnAddress",
              waitOnAddressOriginal, waitOnAddressHook),
  WAIT_TARGET("kernelbase.dll", "SleepConditionVariableCS",
              sleepConditionVariableCsOriginal, sleepConditionVariableCsHook),
  WAIT_TARGET("kernelbase.dll", "SleepConditionVariableSRW",
              sleepConditionVariableSrwOriginal, sleepConditionVariableSrwHook),

  /* Some import tables name kernel32 directly instead of the forwarded
   * kernelbase export. The resolver suppresses duplicate addresses so only
   * one original/hook pair is used for a forwarded export. */
  LOCK_TARGET("kernel32.dll", "EnterCriticalSection",
              enterCriticalSectionOriginal, enterCriticalSectionHook),
  LOCK_TARGET("kernel32.dll", "TryEnterCriticalSection",
              tryEnterCriticalSectionOriginal, tryEnterCriticalSectionHook),
  LOCK_TARGET("kernel32.dll", "AcquireSRWLockExclusive",
              acquireSrwLockExclusiveOriginal, acquireSrwLockExclusiveHook),
  LOCK_TARGET("kernel32.dll", "AcquireSRWLockShared",
              acquireSrwLockSharedOriginal, acquireSrwLockSharedHook),
  LOCK_TARGET("kernel32.dll", "TryAcquireSRWLockExclusive",
              tryAcquireSrwLockExclusiveOriginal, tryAcquireSrwLockExclusiveHook),
  LOCK_TARGET("kernel32.dll", "TryAcquireSRWLockShared",
              tryAcquireSrwLockSharedOriginal, tryAcquireSrwLockSharedHook),
  WAIT_TARGET("kernel32.dll", "WaitForSingleObject",
              waitForSingleObjectOriginal, waitForSingleObjectHook),
  WAIT_TARGET("kernel32.dll", "WaitForSingleObjectEx",
              waitForSingleObjectExOriginal, waitForSingleObjectExHook),
  WAIT_TARGET("kernel32.dll", "WaitForMultipleObjects",
              waitForMultipleObjectsOriginal, waitForMultipleObjectsHook),
  WAIT_TARGET("kernel32.dll", "WaitForMultipleObjectsEx",
              waitForMultipleObjectsExOriginal, waitForMultipleObjectsExHook),
  WAIT_TARGET("kernel32.dll", "SignalObjectAndWait",
              signalObjectAndWaitOriginal, signalObjectAndWaitHook),
  WAIT_TARGET("kernel32.dll", "WaitOnAddress",
              waitOnAddressOriginal, waitOnAddressHook),
  WAIT_TARGET("kernel32.dll", "SleepConditionVariableCS",
              sleepConditionVariableCsOriginal, sleepConditionVariableCsHook),
  WAIT_TARGET("kernel32.dll", "SleepConditionVariableSRW",
              sleepConditionVariableSrwOriginal, sleepConditionVariableSrwHook)
};

#define VOX_FX_TARGET_COUNT (sizeof(vox_fx_targets) / sizeof(vox_fx_targets[0]))

static void voxFxResolveTargets(void) {
  size_t i;
  for (i = 0; i < VOX_FX_TARGET_COUNT; ++i) {
    HMODULE module;
    void *address;
    size_t j;
    int duplicate = 0;
    if (*vox_fx_targets[i].original != NULL)
      continue;
    module = GetModuleHandleA(vox_fx_targets[i].module);
    if (module == NULL)
      continue;
    address = (void *)GetProcAddress(module, vox_fx_targets[i].name);
    if (address == NULL)
      continue;
    for (j = 0; j < VOX_FX_TARGET_COUNT; ++j) {
      if (j != i && *vox_fx_targets[j].original == address) {
        duplicate = 1;
        break;
      }
    }
    if (!duplicate)
      *vox_fx_targets[i].original = address;
  }
}

static void voxFxPatchSlot(void **slot) {
  size_t i;
  const void *current = *slot;
  for (i = 0; i < VOX_FX_TARGET_COUNT; ++i) {
    DWORD oldProtect;
    if (*vox_fx_targets[i].original == NULL ||
        current != *vox_fx_targets[i].original)
      continue;
    oldProtect = 0;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect)) {
      InterlockedExchangePointer(slot, vox_fx_targets[i].hook);
      VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
      FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    }
    return;
  }
}

static void voxFxPatchNamedSlot(void **slot, const char *module, const char *name) {
  size_t i;
  for (i = 0; i < VOX_FX_TARGET_COUNT; ++i) {
    DWORD oldProtect;
    if (*vox_fx_targets[i].original == NULL ||
        lstrcmpiA(module, vox_fx_targets[i].module) != 0 ||
        lstrcmpiA(name, vox_fx_targets[i].name) != 0)
      continue;
    oldProtect = 0;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect)) {
      InterlockedExchangePointer(slot, vox_fx_targets[i].hook);
      VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
      FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    }
    return;
  }
  voxFxPatchSlot(slot);
}

static void voxFxPatchModule(HMODULE module) {
  BYTE *base = (BYTE *)module;
  const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
  const IMAGE_NT_HEADERS *nt;
  const IMAGE_DATA_DIRECTORY *directory;
  const IMAGE_IMPORT_DESCRIPTOR *descriptor;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return;
  nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE)
    return;
  directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (directory->VirtualAddress == 0 || directory->Size == 0)
    return;
  descriptor = (const IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress);
  for (; descriptor->Name != 0; ++descriptor) {
    IMAGE_THUNK_DATA *thunk;
    const IMAGE_THUNK_DATA *lookup;
    const char *importModule;
    if (descriptor->FirstThunk == 0)
      continue;
    thunk = (IMAGE_THUNK_DATA *)(base + descriptor->FirstThunk);
    lookup = descriptor->OriginalFirstThunk == 0
      ? NULL : (const IMAGE_THUNK_DATA *)(base + descriptor->OriginalFirstThunk);
    importModule = (const char *)(base + descriptor->Name);
    for (; thunk->u1.Function != 0; ++thunk) {
      if (lookup != NULL && lookup->u1.Ordinal != 0 &&
          (lookup->u1.Ordinal & IMAGE_ORDINAL_FLAG) == 0) {
        const IMAGE_IMPORT_BY_NAME *imported =
          (const IMAGE_IMPORT_BY_NAME *)(base + lookup->u1.AddressOfData);
        voxFxPatchNamedSlot((void **)&thunk->u1.Function, importModule,
                            (const char *)imported->Name);
        ++lookup;
      } else {
        voxFxPatchSlot((void **)&thunk->u1.Function);
        if (lookup != NULL)
          ++lookup;
      }
    }
  }
}

static void voxFxInstallHooks(void) {
  HMODULE modules[1024];
  DWORD needed = 0;
  DWORD count;
  DWORD i;
  voxFxResolveTargets();
  if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
    return;
  count = needed / (DWORD)sizeof(HMODULE);
  if (count > (DWORD)(sizeof(modules) / sizeof(modules[0])))
    count = (DWORD)(sizeof(modules) / sizeof(modules[0]));
  for (i = 0; i < count; ++i)
    voxFxPatchModule(modules[i]);
}

int voxFxLockAvailable(void) {
  return 1;
}

void voxFxLockPrepare(void) {
  vox_fx_lock_active = 0;
  voxFxInstallHooks();
}

void voxFxLockBegin(void) {
  vox_fx_lock_count = 0;
  vox_fx_wait_count = 0;
  vox_fx_lock_active = 1;
}

unsigned long voxFxLockEnd(void) {
  vox_fx_lock_active = 0;
  return vox_fx_lock_count;
}

unsigned long voxFxWaitEnd(void) {
  vox_fx_lock_active = 0;
  return vox_fx_wait_count;
}
