# Realtime allocation probe

The probe counts the allocation calls made by **one thread**, the thread that
calls `voxProbeBegin()` (the test's audio thread). It is declared in
`tests/support/AllocationProbe.h`. Tests that call it link it through
`vox_add_allocation_probe(<target>)` in `CMakeLists.txt`, which also defines
`VOX_TEST_INTERPOSE_MALLOC`.

Other threads never count into the probed thread's result. This covers the C2
sample loader worker, the controller thread that publishes and collects sample
buffers, and the message thread. A background thread that probes itself gets
its own count.

## Platforms

| Platform | Mechanism | Covered entry points |
|---|---|---|
| Linux (glibc) | Symbol interposer in the test executable, linked with `-rdynamic` so dynamically loaded modules resolve to it | `malloc`, `calloc`, `realloc`, `reallocarray`, `posix_memalign`, `aligned_alloc`, `memalign`, `valloc`, `pvalloc` |
| Windows (MSVC) | Import address table hooks in every loaded module, re-applied by each `voxProbeBegin()`; slots matched by address, so forwarders and API sets are covered | ntdll `RtlAllocateHeap`, `RtlReAllocateHeap`, `NtAllocateVirtualMemory(Ex)`; kernelbase/kernel32 `VirtualAlloc(Ex, ExNuma, 2)`, `LocalAlloc`/`LocalReAlloc`, `GlobalAlloc`/`GlobalReAlloc`; ucrtbase `malloc`, `calloc`, `realloc`, `_recalloc`, `_expand`, `_malloc_base`, `_calloc_base`, `_realloc_base`, `_aligned_malloc`, `_aligned_realloc`, `_aligned_recalloc`, `_aligned_offset_malloc`; combase/ole32 `CoTaskMemAlloc`, `CoTaskMemRealloc` |

The other C and C++ allocation routes all reach one of these entry points:
- C++ `operator new` and `new[]`, aligned and `nothrow` new;
- the standard containers, `std::function` and `std::make_shared`;
- a static CRT (`/MT`, as in the instrument DLLs), through its own `HeapAlloc`
  import.

The probe counts entry points entered, not allocations, so one `malloc` can
count more than once (for example, the CRT and then the OS heap). A probed
audio path must count exactly 0.

In ThreadSanitizer builds (`VOX_SANITIZE_THREAD`), TSan intercepts `malloc`
itself. The probe therefore counts through the sanitizer allocator's malloc
hook instead (`tests/support/AllocationProbeSanitizer.c`).
- That hook does not run for `posix_memalign`, `aligned_alloc`, `memalign` or
  `valloc`, so `allocation_probe_tests` reports those four as not covered in
  that build only.
- `native_rack_tests` still skips its zero-allocation checks under TSan.

## Self-test: `allocation_probe_tests`

This test runs wherever the probe is built. It proves, on the machine that
runs it, that each path is counted on the probed thread:
- the standard paths listed above;
- the platform's OS heap and virtual-memory paths;
- allocations inside a separately loaded module (`vox_allocation_canary`). On
  Windows that module uses the static CRT (`/MT`) and is loaded after the
  earlier probes.

It also proves that:
- arithmetic on preallocated memory counts 0;
- a background thread allocating all the time does not show up in the probed
  thread's count, while a background thread that probes itself does count.

The multi-thread case is free of data races: its anti-elision sink is
per-thread. A ThreadSanitizer build of `allocation_probe_tests` shows this
(5 runs, 0 reports, after the review 5755905326 fix).

## Realtime gates that use it

| Test | What runs under the probe |
|---|---|
| `module_abi_v2_tests` | 1000 module `process` and 10 `reset` calls |
| `native_rack_tests` | Rack blocks with a native slot |
| `demo_synth_tests` | Rack blocks with Vox Demo Synth |
| `fullon_drums_tests` `noAllocationWhilePlaying` | 400 Rack blocks: synth and sample voices, filter, transient, output bus, sends, automation |
| `fullon_drums_tests` `realtimeAllocationsWithSampleSwaps` | ≥ 1500 Rack blocks of changing size (1..512), described below |

`realtimeAllocationsWithSampleSwaps` covers the following, all running at once:
- sample voices: one-shot, loop, reverse, normalize and Layer;
- LP 24 and BP filters, transients, sends and an output bus;
- the internal sequencer in Both mode on a running host transport;
- host notes at nonzero offsets and automation;
- at the same time, a controller thread that publishes new sample buffers and
  collects retired ones, while the real C2 loader decodes WAV files.

The test also checks that buffers were actually swapped and retired, and that
the loader installed files, during the probed window.

Mutation evidence (Linux, local):

| Mutation | Result |
|---|---|
| An allocation when a swapped buffer is installed (`SampleExchange::beginBlock`) | Fails only `realtimeAllocationsWithSampleSwaps` |
| An allocation in the module's sample voice | Fails the Drums probes |
| `posix_memalign` not counted | `allocation_probe_tests` fails: aligned new uses it |
| Counts not per thread | `allocation_probe_tests` fails |

## Limits

These limits are documented in `AllocationProbeWindows.c`.

**Windows.** The following are not seen:
- allocations that ntdll makes internally without an import slot;
- unbound delay-load imports;
- modules loaded after the last `voxProbeBegin()`;
- import calls the OS has rewritten into direct calls (import call
  optimization).

Missing a path makes the count lower, never higher. `allocation_probe_tests`
shows which paths are counted on the CI machine.

**Both platforms.** The probe sees heap and virtual-memory allocation only.
Locks, blocking system calls and page faults need other gates. Linux has the
`rack_realtime_boundary_tests` `--wrap` gate.

A green probe in CI is test evidence only. It is not a DAW measurement.
