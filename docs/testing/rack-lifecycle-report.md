# Rack state and lifecycle validation

Date: 25.10.2026. Base main: e865042d414a974d0cbc62e1b7e78e62ec2ed5fb.
This change is independent of the MIDI-overflow fix in PR #46 and AGY's Bass task #45.

## Confirmed defects

1. Direct Rack::replaceState accepted duplicate SlotId values and malformed metadata
   that serialized project input rejected. Empty/missing/newer-schema slots could bypass
   host payload checks.
2. Serialized level/pan were not checked for finite values and legal ranges at decode.
3. Rack::prepare accepted NaN/Inf sample rates and could leave a partially prepared
   collection of modules/buffers after a provider failure.
4. Stereo processing after mono prepare could touch an unallocated second scratch channel.
5. The process path read spec_ before joining the control/audio mutation gate.

The first real-Rack regression failed on the old source:
"Accepted invalid state: duplicate empty SlotId".
After state validation was fixed, the expanded prepare regression separately failed:
"nonfinite sample rate is rejected before changing the live spec".

## Changes

- Shared control-thread validateSlot/validate for typed loads and decoded projects.
- Check nonzero unique IDs, stable identity/parameter strings, preset-name byte limits,
  finite/ranged mix/macros, routing/output, pattern schema and host payload ceilings.
- Validate all typed slots before any provider construction.
- Preserve raw integer checks before narrowing and encoded-size checks before decoding.
- Commit parsed output only after complete validation.
- Legal missing modules and future pattern schemas remain recoverable opaque state.
- Module loads and pattern updates cannot publish invalid identity/name metadata.
- Reject invalid prepare inputs before changing the working rack.
- Stage scratch allocation before touching modules; allocation failure preserves the valid runtime.
- Serialize prepare on the existing control mutex and gate module/spec/buffer changes.
- If a provider returns false or throws after preparation begins, reset transient state
  and keep spec empty. The rack adds no audio until a later successful prepare.
- Rebind scratch pointers only after successful preparation; clear unused mono pointers.
- Place spec/output validation inside an RAII active-process guard.
- Use sequentially consistent gate/counter operations so the two-atomic exclusion handshake
  cannot admit both mutation and processing through stale observations.

Provider failure is intentionally **fail-closed**, not a claimed rollback of provider internals.
It can stop all rack voices until successful reprepare. Invalid input or pre-mutation
allocation failure preserves the old valid runtime.
Rack processing remains additive: no contribution is added on refusal; caller-owned mix
buffers are not erased.

Staging temporarily holds old and replacement scratch buffers, increasing peak
control-thread memory during reprepare. No end-user CPU/RAM guarantee is claimed.

## Actual validation

- New lifecycle suite through actual Rack.cpp/RackState.cpp/Router/Registry/Modulation
  and test-only ReferenceInstrument: **406 checks PASS**.
- Covers 31 malformed typed-state mutations, preflight before construction,
  unchanged persistent state and held-note ownership after refusal,
  finite/ranged XML mix state and transactional destination output.
- Covers channel isolation, mute/unmute, solo, unload/reload, fresh-instance restore,
  missing/future content round-trip, sample-rate changes and mono/stereo mismatch.
- Injects second-slot prepare false/exception while a third slot is loaded;
  checks failed larger-block prepare stays silent and a retry restores operation.
- Concurrent churn: 32 prepare operations overlapping 1,200 callback attempts,
  with immutable control snapshots; finite output and a usable gate afterwards.
- Project-source ASan/UBSan build: **406 checks PASS**. JUCE dependency objects are
  standard objects, so this is not a fully instrumented JUCE-framework claim.
- Windows Release/full CTest/package and actual Cubase/REAPER checks remain required.

The actual dependency is the existing pinned JUCE 9.0.1, commit
e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8. No framework version or packaging changes.

## Local build

No audio device, plugin GUI or DAW is used.
Compile JUCE core, core_CompilationTime, events and data_structures objects from
the pinned checkout with C++20, JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1,
JUCE_STANDALONE_APPLICATION=1 and JUCE_USE_CURL=0.
Then link these actual project sources:

```text
libs/rack/Rack.cpp
libs/rack/RackRouter.cpp
libs/rack/RackState.cpp
libs/instrument/HostParameterSchema.cpp
libs/instrument/InstrumentRegistry.cpp
libs/instrument/ModuleState.cpp
libs/modulation/Modulation.cpp
libs/modulation/ModulationState.cpp
tests/support/ReferenceInstrument.cpp
tests/rack/RackLifecycleTests.cpp
```

Include libs/, tests/ and the JUCE modules directory.
Local GCC 13.3: -std=c++20 -O1 -DNDEBUG -Wall -Wextra -Wpedantic,
with pthread/dl/rt/zlib. Sanitized project build adds
-fsanitize=address,undefined -fno-omit-frame-pointer.
Leak detection is disabled separately because sandbox /proc restrictions
prevent LeakSanitizer process inspection; no leak-test pass is claimed.

CMake adds rack_lifecycle_tests to the existing Windows CTest suite.
No test-only provider is registered or packaged in the production plugin.

After the normal Windows configure, run the focused target with:

```powershell
cmake --build build --config Release --target rack_lifecycle_tests
ctest --test-dir build -C Release -R "^rack_lifecycle_tests$" --output-on-failure
```

These Windows commands are instructions; their execution is not claimed by this report.

## Limits and follow-up

- No changed plugin identity, host parameter IDs, external ABI or persistent schema.
- No GUI/artwork, Bass DSP/provider/processor or generation changes.
- CMake changes only register this test target.
- setMacro plain-value mapping and live-control synchronization need separate work;
  current applications do not call those direct Rack control methods.
- This concurrency test is not ThreadSanitizer proof or full DAW acceptance.
- Invalid oversized state is intentionally rejected; valid old serialized schema
  migration and legal unresolved module behaviour remain supported.
- Fully transactional reconstruction after a provider's failed prepare is not promised.

## AgentReport

```yaml
agent_report:
  role: Lead / Rack integration
  task: validate state before construction and make reprepare failure safe
  result: PARTIAL
  files_changed:
    - libs/rack/Rack.cpp
    - libs/rack/RackState.cpp
    - libs/rack/RackState.h
    - tests/rack/RackLifecycleTests.cpp
    - CMakeLists.txt (one test target)
    - docs status/index and lifecycle report
  contracts_touched:
    - control-thread state admission and prepare/process lifecycle
    - no external ABI, schema or identity changes
  assumptions:
    - one audio-processing caller per rack; control mutations use the existing mutex/gate
  implementation:
    - shared preflight and transactional decoded output
    - staged prepare, fail-closed provider failure and gated spec reads
  tests_added:
    - actual Rack lifecycle suite, 406 checks
  verification:
    - command: actual Rack + JUCE 9.0.1 lifecycle executable
      result: PASS
    - command: project-source ASan/UBSan lifecycle executable
      result: PASS
      notes: 406 checks; dependency objects not instrumented; leak detection unavailable
    - command: Windows Release / full CTest / package / DAW
      result: NOT_RUN
  architecture_changes:
    - lifecycle fail-closed policy, reviewed independently
  migration_impact:
    - none for valid project data; malformed typed state now rejected
  performance_impact:
    - no callback locks or allocations added by inspection
    - temporary old/new scratch overlap during control-thread prepare
  security_impact:
    - metadata/payload checks also cover unresolved modules
  manual_acceptance_required:
    - actual DAW lifecycle, restore, automation and audio acceptance
  unresolved:
    - required CI, full plugin validation and live-control API follow-up issue 47
  risks:
    - provider prepare failure silences the rack until successful retry
```

Merge recommendation: WAIT for required Windows CI.
Manual gate: REQUIRED for functional release claims.
Next task: live-control API synchronization and plain macro mapping, then integrated host validation.
