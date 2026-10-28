# Instrument module ABI V2 — proposal for Architecture Guard review

Status: **PROPOSED**, 28.10.2026. No loader code exists. This document is the
Rack owner's answer to the #62 architecture follow-up and builds on
[independent instruments](https://github.com/ultima-vox/vox-electronic-engine/blob/docs/independent-instruments/docs/architecture/INDEPENDENT_INSTRUMENTS.md)
(#63) and the Acid V2 integration packet (#59). It supersedes the data-only
[instrument packages](INSTRUMENT_PACKAGES.md) proposal.

## Owner decisions this design implements

| Decision (28.10.2026) | Consequence |
| --- | --- |
| A new compatible DSP instrument ships without rebuilding Rack | Native module (DLL) behind a stable, versioned C ABI |
| Rack is the only parameter authority | Modules never register host parameters; the Rack common adapter owns registration, normalized/plain conversion, gestures and state |
| Replacing an instrument must not silently reuse automation | Explicit replacement confirmation + per-slot stale-automation inhibit, explicit rearm |
| Same-instrument upgrades | #65 strict pool indices + append-only migration |
| Keep the Rack simple | 16 slots, one MIDI channel per instrument, shared rail, one instrument page, VOX widgets; no arbitrary VST hosting, no Kontakt-style complexity |

## Non-goals

No hot DLL unload, no crash isolation (an in-process DLL failure is a DAW
failure), no third-party plugin hosting, no licensing/encryption, no visual
redesign. V1 (`InstrumentAbi.h`) is not mutated; V2 is a separate header.

## Package layout (Windows 10/11 x64 first)

```
<packId>.voxinst/
  manifest.json      packId, instrumentIds, versions, abi range, arch, files + SHA-256
  module/x64/<name>.dll
  ui/                declarative workspace (ui.json) + images/fonts
  content/           Sound presets, Pattern presets, Generator profiles
```

Proposed roots: `%PROGRAMDATA%\Ultima Vox\Instruments` (installer) and
`%APPDATA%\Ultima Vox\Instruments` (user); macOS equivalents later. A pack
installs into a versioned folder and becomes active on the next Rack instance
construction (no swap under a live instance).

## ABI V2 rules

- Pure C header (`VoxModuleAbiV2.h`), compilable as C11 and C++20. No STL, JUCE,
  exceptions, `bool`, `noexcept` or C++ types in function-pointer signatures.
- Every struct starts with `uint32_t structSize; uint32_t version;`. A reader
  accepts a larger `structSize` (newer minor version) and rejects a smaller one.
  New fields only append; meanings never change.
- Every function returns `VoxResult` (`int32_t`, 0 = OK, negative = error codes);
  a module must catch everything internally.
- Memory: the module owns what it allocates and frees it itself. Strings and
  arrays passed by the host are valid only during the call. Descriptor memory
  returned by the module stays valid until the module is unloaded; the host
  deep-copies and validates it once at scan time.
- Thread classes are part of the contract (table below); RT functions must not
  allocate, lock, do I/O or call back into the host except `VoxRtServicesV2`.

## Entry point and module object

```c
/* Exported by the DLL. Called once per process on the message thread. */
VOX_EXPORT VoxResult voxModuleEntryV2(uint32_t hostAbiVersion,
                                      const VoxHostServicesV2* host,
                                      const VoxModuleV2** module);

typedef struct VoxModuleV2 {
  uint32_t structSize, version;      /* VOX_MODULE_ABI_V2 = 2 */
  uint32_t instrumentCount;
  const VoxInstrumentDescriptorV2* (*instrumentAt)(uint32_t index);
  VoxResult (*create)(const char* instrumentId, const VoxCreateParamsV2*,
                      VoxInstance* out);
  void      (*destroy)(VoxInstance);
  VoxResult (*prepare)(VoxInstance, const VoxPrepareSpecV2*);   /* non-RT */
  void      (*reset)(VoxInstance);                                /* RT-safe */
  void      (*process)(VoxInstance, const VoxProcessContextV2*);  /* RT */
  VoxResult (*saveState)(VoxInstance, uint8_t* buffer, uint32_t capacity,
                         uint32_t* written, uint32_t* stateVersion);
  VoxResult (*loadState)(VoxInstance, uint32_t stateVersion,
                         const uint8_t* bytes, uint32_t size);
  const VoxContentApiV2* content;    /* presets, patterns, generator; may be NULL */
  const VoxEditorApiV2*  editor;     /* optional native editor; NULL = declarative UI */
  void      (*shutdown)(void);       /* host teardown, after all instances died */
} VoxModuleV2;
```

### Descriptor

`VoxInstrumentDescriptorV2` keeps V1 identity/version/budget fields and adds:
`latencySamples`/`tailSamples` declarations, `parameterCount` and

```c
typedef struct VoxParameterDescriptorV2 {
  uint32_t structSize, version;
  const char* id; const char* name; const char* unit; const char* group;
  uint32_t type;                 /* float, integer, boolean, choice */
  float minimum, maximum, defaultValue, step;
  const char* const* choices; uint32_t choiceCount;
  int32_t poolIndex;             /* 0..pool-1 when hostExposed, else -1 */
  uint32_t flags;                /* HOST_EXPOSED, MODULATABLE, AUTOMATABLE */
} VoxParameterDescriptorV2;
```

Validation at scan (all or nothing for the pack): unique instrument IDs across
installed packs (conflict = both rejected, never "pick one"), unique parameter
IDs, unique in-range `poolIndex` for host-exposed parameters, typed range rules
already enforced for direct parameters, budgets within `HostBudgetLimits`.

### Parameter path (RT)

Parameters are addressed by **descriptor index**, resolved once off the audio
thread; no string lookup in callbacks (fixes the per-block linear search noted
for Acid). `VoxProcessContextV2` carries a bounded, sample-offset-sorted list of
changed parameters for this block:

```c
typedef struct VoxParamChangeV2 { uint32_t index; uint32_t sampleOffset; float plain; } VoxParamChangeV2;
```

The host sends only values that changed (dirty publication), in plain units of
the descriptor. Base value, host automation and modulation stay distinct: the
host sends base values; a module applies its own modulation internally and
never reports values back as host edits.

### Editor and UI

Default: declarative `ui.json` rendered by host-owned VOX widgets (knob, value,
choice, toggle, pill, step sequencer, envelope graph), bound by parameter ID.
Optional `VoxEditorApiV2` for a reviewed native editor: `create(parentHwnd,
scale, VoxEditorHostV2*) -> handle`, `setBounds`, `setScale`, `idle`,
`destroy`; gestures go through `VoxEditorHostV2.beginEdit/performEdit/endEdit
(index, plain)` on the message thread only. No JUCE/C++ objects cross. The Bass
UI uses the same instrument-page grammar as every instrument.

## Threads

| Call | Thread | May allocate / lock / I/O |
| --- | --- | --- |
| `voxModuleEntryV2`, `instrumentAt`, `content` queries, `shutdown` | message | yes |
| `create`, `destroy`, `prepare`, `saveState`, `loadState` | controller (message), audio suspended for the slot | yes |
| `process`, `reset` | audio | no |
| `VoxEditorApiV2` | message | yes (not in audio) |

## Lifecycle

1. **Scan** (message thread, construction or explicit rescan): read manifests,
   verify file hashes, `LoadLibrary`, call entry, deep-copy and validate
   descriptors, register into `InstrumentRegistry`. Failures are reported per
   pack; nothing partial is registered.
2. **Lease**: every instance, descriptor copy and editor holds a module lease.
   Initial policy: a module stays loaded until processor teardown; no unload while
   any lease exists; `shutdown` then `FreeLibrary` after the last lease.
3. **Missing / incompatible module**: existing unresolved-slot policy (identity,
   named values, patterns and opaque payload preserved; silence; no substitution).
4. **Update**: a new pack version is picked up by the next Rack instance; open
   projects then use #65 append-only migration or stay unresolved.

## Instrument replacement and automation

Replacing an occupied slot with a **different** InstrumentId:

1. UI shows a confirmation naming the old and new instrument and stating that
   the slot's automation lanes will be inhibited.
2. On confirm, the slot is marked `automationInhibited` (persisted). While set,
   host writes to that slot's pool endpoints are not applied to DSP; the Rack's
   named values drive the instrument. The slot header shows "Automation paused"
   with a **Rearm** action.
3. Rearm clears the flag; lanes then drive the new instrument at their indices.
4. Missing modules never trigger replacement. Same-InstrumentId upgrades never
   inhibit.

Feasibility to verify in Cubase 12–15 and REAPER: hosts keep writing while
inhibited, lanes may visually disagree with the sound until rearm; latch/touch
gestures; save/reopen keeps the flag.

## Parameter capacity (decision needed before freezing the VST3 layout)

Acid needs 80 independent host controls; the pool gives 64 (+8 macros, which
stay independent performance assignments, not shadow copies). Options:

| Pool per slot | Total endpoints | Acid margin |
| --- | --- | --- |
| 64 (current #65) | 1024 | -16 — fails |
| 96 | 1536 | 16 |
| 128 | 2048 | 48 |

Recommendation: decide from measurement, not intuition — processor
construction time, VST3 parameter enumeration and project load in Cubase/REAPER
at 1024/1536/2048, plus idle per-block cost. Parameters beyond the pool may exist
with `HOST_EXPOSED` cleared (UI-only), never as fake host knobs.

## Performance budget (to measure before optimizing)

1/2/8/16 active modules, empty/idle slots, FX returns and ducking at 44.1/48/96
kHz and 64/128/512-sample blocks: callback p95/p99/worst, allocations (must be
0), peak memory, module creation and project restore time, host parameter
enumeration time. No per-sample O(all host parameters) work.

## Delivery slices (each with tests, each reviewed)

1. `VoxModuleAbiV2.h` + static validation tests (struct sizes, version rules)
   and an in-process fake module behind the V2 table (no DLL).
2. Test module built as a real shared library in CMake; loader scan/validate/
   lease; missing/corrupt/conflicting pack tests.
3. Index-based dirty parameter publication; RT allocation probe.
4. Replacement confirmation + automation inhibit/rearm with state round-trip.
5. Declarative `ui.json` renderer for the common widget set.
6. Acid packaged by the Acid agent against the frozen V2 (after capacity decision).

## Open decisions for the owner

1. Pool capacity 64 / 96 / 128 (after measurement).
2. Declarative UI only, or also the native editor interface for Acid's 303 view.
3. Install roots and activation (next instance vs explicit rescan).
4. Pack authenticity (hash only vs signing) — out of scope until asked.
