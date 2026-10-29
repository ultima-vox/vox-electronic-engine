# Instrument module ABI V2 — proposal for Architecture Guard review

Status: **PROPOSED, NOT FROZEN**, 28.10.2026. No loader code exists. This
document is the Rack owner's answer to the #62 architecture follow-up and builds
on
[independent instruments](https://github.com/ultima-vox/vox-electronic-engine/blob/docs/independent-instruments/docs/architecture/INDEPENDENT_INSTRUMENTS.md)
(#63) and the Acid V2 integration packet (#59). It supersedes the data-only
[instrument packages](INSTRUMENT_PACKAGES.md) proposal. Revision 2 answers the
owner's Architecture Guard-style review of `467d854`. The ABI may be called
frozen only after the gates in [Freeze gates](#freeze-gates) pass. #63 is
reconciled against this document once its docs merge.

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

## Package layout

Superseded by [VINS_PACKAGES.md](VINS_PACKAGES.md): one `.vins` ZIP archive
file per instrument, next to the VST3. A native package carries
`module/win-x64/<name>.dll` plus a publisher signature over `manifest.json`.
The earlier `<packId>.voxinst/<version>/` folder layout no longer exists.

## Binary contract

### Language, calling convention, layout

- One header, `VoxModuleAbiV2.h`, valid C11 and C++20 (`extern "C"` guarded).
  No STL, JUCE, exceptions, `bool`, C++ references or C++ types cross it.
- `VOX_CALL` is `__cdecl` on Windows (x64 has a single convention; the macro
  makes it explicit and keeps x86/ARM64 honest). `VOX_EXPORT` is
  `__declspec(dllexport)`; the only exported symbol is `voxModuleEntryV2`.
- Fixed-width types only (`int32_t`, `uint32_t`, `uint64_t`, `float`,
  `double`, pointers). Enumerations travel as `uint32_t`. Booleans are
  `uint32_t` 0/1.
- Natural alignment; no `#pragma pack`, no bit-fields. Field order is chosen
  so that x64 layout has no implicit padding; every struct size is a multiple
  of 8. Header ships `_Static_assert`/`static_assert` checks for `sizeof` and
  `offsetof` of every field, compiled in both languages.
- Opaque handles: `typedef struct VoxInstanceT* VoxInstance;` and
  `typedef struct VoxEditorT* VoxEditor;`. The host never dereferences them.

### Two kinds of struct

1. **Versioned structs** (module table, descriptors, prepare spec, process
   context, service tables) start with `uint32_t structSize; uint32_t version;`.
   A reader accepts a larger `structSize` (newer minor version, trailing fields
   ignored) and rejects a smaller one or an unknown major `version`. Fields only
   append; meanings never change.
2. **Flat event records** (`VoxParamChangeV2`, `VoxMidiEventV2`) carry **no
   header**: they are elements of arrays inside a versioned struct, which also
   carries the element stride (`paramChangeStride`, `midiEventStride`). Readers
   step by the stride, so records may grow by appending fields in a later minor
   version. These two records are the only header-less structs.
3. **Arrays and strides.** Every array the other side walks (parameter
   descriptors, change and MIDI records) is passed with an element stride that
   must be `≥ sizeof(record)`, a multiple of `alignof(record)` and at most
   1024 bytes; the array pointer must be aligned. A versioned record's own
   `structSize` must not exceed the stride. Counts are bounded before any
   arithmetic or access. Readers copy header-less records out with `memcpy`
   instead of dereferencing them in place.

### Results and faults

- **Every function pointer returns `VoxResult`** (`int32_t`): `VOX_OK = 0`,
  negative errors: `VOX_E_INVALID_ARG`, `VOX_E_UNSUPPORTED`,
  `VOX_E_BUFFER_TOO_SMALL`, `VOX_E_STATE_VERSION`, `VOX_E_STATE_CORRUPT`,
  `VOX_E_OUT_OF_MEMORY`, `VOX_E_INTERNAL`. No `void` functions and no raw
  pointer returns; queries return through out-parameters.
- A module catches everything internally; nothing unwinds across the boundary.
- `process`/`reset` return codes are read on the audio thread without
  allocation. A non-zero result silences that slot for the block, sets the
  slot's atomic fault flag and bumps a fault counter; the message thread later
  reports it and keeps the slot silent until reprepare. `destroy`/`shutdown`
  errors are logged; the host still treats the object as gone.

### Ownership and lifetime

| Memory | Owner | Valid until |
| --- | --- | --- |
| Descriptors, choice arrays, strings returned by `getInstrument` | module | `shutdown` returns; host deep-copies at scan and never keeps module pointers |
| Arguments passed by the host (strings, buffers, process context, event arrays, audio channel pointers) | host | the call returns |
| `VoxInstance`, `VoxEditor` | module | the matching `destroy` |
| State bytes | caller-provided buffer | the call returns |

### State serialization

- The descriptor declares `maxStateBytes` (≤ host `maxStateBytesPerSlot`,
  1 MiB). The host passes a buffer of exactly that capacity, preallocated
  off the audio thread.
- `saveState(inst, buffer, capacity, &written, &stateVersion)`: `VOX_OK` with
  `written ≤ capacity`, or `VOX_E_BUFFER_TOO_SMALL` with `written` = required
  size, which the host treats as a module bug (save fails, previous state kept;
  no retry loop).
- `loadState(inst, stateVersion, bytes, size)`: `VOX_E_STATE_VERSION` for a
  newer version, `VOX_E_STATE_CORRUPT` for malformed bytes; on either the slot
  stays unresolved with its payload preserved, as today.
- Both run on the controller thread with that slot's audio suspended.

## Entry point and module object

```c
/* The only export. Called once per loaded DLL per process (see Lifecycle). */
VOX_EXPORT VoxResult VOX_CALL voxModuleEntryV2(uint32_t hostAbiVersion,
                                               const VoxHostServicesV2* host,
                                               const VoxModuleV2** module);

typedef struct VoxModuleV2 {
  uint32_t structSize, version;      /* VOX_MODULE_ABI_V2 = 2 */
  uint32_t instrumentCount, reserved0;
  VoxResult (VOX_CALL *getInstrument)(uint32_t index,
                                      const VoxInstrumentDescriptorV2** out);
  VoxResult (VOX_CALL *create)(const char* instrumentId,
                               const VoxCreateParamsV2*, VoxInstance* out);
  VoxResult (VOX_CALL *destroy)(VoxInstance);
  VoxResult (VOX_CALL *prepare)(VoxInstance, const VoxPrepareSpecV2*);    /* controller */
  VoxResult (VOX_CALL *setAllParameters)(VoxInstance, const float* plain,
                                         uint32_t count);                 /* controller */
  VoxResult (VOX_CALL *reset)(VoxInstance);                                /* audio */
  VoxResult (VOX_CALL *process)(VoxInstance, const VoxProcessContextV2*);  /* audio */
  VoxResult (VOX_CALL *saveState)(VoxInstance, uint8_t* buffer, uint32_t capacity,
                                  uint32_t* written, uint32_t* stateVersion);
  VoxResult (VOX_CALL *loadState)(VoxInstance, uint32_t stateVersion,
                                  const uint8_t* bytes, uint32_t size);
  const VoxContentApiV2* content;    /* presets, patterns, generator; may be NULL */
  const VoxEditorApiV2*  editor;     /* optional native editor; NULL = declarative UI */
  VoxResult (VOX_CALL *shutdown)(void);  /* after the last lease, before FreeLibrary */
} VoxModuleV2;
```

### Descriptor

`VoxInstrumentDescriptorV2` keeps V1 identity/version/budget fields and adds
`latencySamples`/`tailSamples` declarations, `parameterCount` and

```c
typedef struct VoxParameterDescriptorV2 {
  uint32_t structSize, version;
  const char* id; const char* name; const char* unit; const char* group;
  const char* const* choices;
  uint32_t type;                 /* float, integer, boolean, choice */
  uint32_t choiceCount;
  float minimum, maximum, defaultValue, step;
  int32_t poolIndex;             /* 0..pool-1 when HOST_EXPOSED, else -1 */
  uint32_t flags;                /* HOST_EXPOSED, MODULATABLE, AUTOMATABLE */
} VoxParameterDescriptorV2;
```

Validation at scan (all or nothing for the pack): unique instrument IDs across
installed packs (conflict = both rejected, never "pick one"), unique parameter
IDs, unique in-range `poolIndex` for host-exposed parameters, typed range rules
already enforced for direct parameters, budgets within `HostBudgetLimits`.
Typed rules: strings non-null and at most 256 bytes; finite `minimum <
maximum`, default in range; float `step` from 0 (continuous) up to the range;
integer, boolean and choice `step` exactly 1 with integral bounds; boolean is
0..1; choice is `0..choiceCount-1` with `choiceCount` 2..128 checked before any
`choices[i]` is read. `parameterCount == 0` is valid (an instrument with no
controls); descriptor and budgets are still validated and no dummy endpoints
are created. Stride and alignment rules above apply. Slice 1's harness
validator implements and negative-tests these rules.

## Realtime parameter and event contract

### Accuracy (explicit)

**V2 parameter changes are block-rate.** The JUCE 9.0.1 VST3 wrapper
(`processParameterChanges`) applies only the last point of each
`IParamValueQueue` before `processBlock`; APVTS readouts therefore do not prove
sample-accurate automation. Every V2 change is delivered with
`sampleOffset = 0`. The `sampleOffset` field exists so a later host path can
become sample-accurate without an ABI break; modules must already accept any
offset in `[0, numSamples)`. MIDI events stay sample-accurate (they carry the
host's offsets).

### Records and bounds

```c
typedef struct VoxParamChangeV2 { uint32_t index; uint32_t sampleOffset; float plain; uint32_t reserved; } VoxParamChangeV2;
typedef struct VoxMidiEventV2   { uint32_t sampleOffset; uint32_t size; uint8_t bytes[8]; } VoxMidiEventV2;
```

- `index` is the **descriptor index**, resolved once off the audio thread; no
  string lookup in callbacks (fixes Acid's per-block linear search).
- **Snapshot:** after `prepare`, `loadState` or rearm the host calls
  `setAllParameters` (controller thread, slot audio suspended) with every
  current plain value, so a module never depends on having seen earlier
  deltas. `process` then carries only deltas.
- **`reset` keeps parameters.** `reset` (audio thread) clears DSP state only:
  voices, envelopes, filters, delay lines, sequencer position. It must keep the
  module's current plain parameter values, so the last snapshot plus the deltas
  already delivered stay valid and no resnapshot is needed. If the host ever
  needs a resnapshot from audio context, it sets a preallocated per-slot atomic
  request; the controller thread services it with the slot suspended. Controller
  functions are never called from `reset` or `process`. A conformance test
  checks that output after `reset` uses the pre-reset parameter values.
- **Capacity:** `VoxPrepareSpecV2.maxParamChangesPerBlock = parameterCount`
  (≤ `poolParametersPerSlot` + UI-only parameters). The host-side change array
  is preallocated in `prepare`.
- **Ordering and duplicates:** sorted by `(sampleOffset, index)` ascending; at
  most one record per `(sampleOffset, index)`; the host coalesces duplicates,
  last write wins.
- **Overflow:** cannot occur for block-rate delivery (≤ one record per
  parameter). For a future sample-accurate path the host coalesces each index
  to its final value of the block when the array would overflow, so no final
  value is ever dropped, and increments a coalescing counter. Nothing is
  silently misapplied: an out-of-range index or non-finite value is rejected by
  the host before the call.
- **MIDI:** see [Bounded MIDI and release policy](#bounded-midi-and-release-policy).
- **Base vs automation vs modulation:** the host sends base values (named
  value or, when armed, the automation-driven endpoint value). A module applies
  its own modulation internally and never reports values back as host edits.

### Bounded MIDI and release policy

A fixed queue cannot deliver an unbounded event stream exactly. The promise is
therefore **"every owned note terminates"**, not "every release event is
delivered".

- **Ownership.** One instrument = one MIDI channel, so each event routes to at
  most one of the 16 slots; events on unowned channels are discarded before
  queueing. Per slot the host keeps a preallocated 128-bit *sounding set* of
  notes it has delivered NoteOn for and not yet released (16 × 16 bytes).
- **Queues.** Each slot has its own preallocated queue of capacity
  `C = maxMidiEventsPerBlock` (descriptor budget, 16 ≤ C ≤ 2048; total for 16
  slots ≤ 512 KiB). No allocation or lock in routing.
- **Key semantics (binding for every V2 instrument).** The sounding set
  tracks *keys* (owned channel × pitch), not voices. A NoteOn for a key that is
  already sounding **retriggers or replaces** that key; it never stacks a second
  same-pitch voice. One NoteOff releases the key, however many NoteOns preceded
  it. This matches Acid today. Polyphonic modules may steal voices across
  different keys internally; stealing never changes host key state, and a
  stolen key still ends on its NoteOff. Overlapping same-pitch voices (a bounded
  per-key counter) are out of scope for V2 and would need a minor version.
- **Sustain (CC64).** CC64 is passed through. A NoteOff while sustain is down
  still clears the host key bit; the module keeps the voice until CC64 goes
  below 64. The host tracks `sustainDown` (one bit per slot) and reserves one
  event for the CC64 release. A CC64 press is admitted under the same invariant as a
  NoteOn; if it does not fit it is dropped (notes then release normally, so
  nothing can hang).
- **Release reserve (NoteOn admission).** Invariant per slot and block:
  `used + |sounding| + sustainDown + 1 ≤ C`. A NoteOn is queued only if the invariant still
  holds after it; otherwise it is dropped and counted. Hence there is always
  queue room for one NoteOff per sounding note plus one all-off record.
- **NoteOff.** For a sounding note it always fits (it uses reserved room) and
  clears the bit. For a note not sounding it is redundant, dropped and counted.
- **Rack Panic (UI/host action).** Panic is asynchronous to audio: the
  message thread sets a preallocated atomic. The next callback consumes it and
  passes `VOX_PANIC_AT_START` in `VoxProcessContextV2.releaseFlags` to every
  occupied slot, **before any queued event of that block** (sample 0). The
  module silences all voices and tails immediately (no release phase), then
  processes the block's queued events normally; NoteOns later in the same block
  sound. Panic never uses the queue and does not depend on its fill level.
  "At most one callback" is callback sequencing, not a wall-clock bound: while
  the host suspends processing no callback runs, which is safe (no audio is
  produced). A pending Panic is consumed by the first callback after resume,
  before any event queued for that callback.
- **MIDI all-off events (CC123 All Notes Off, CC120 All Sound Off).** Queued at
  their sample offset when room exists, clearing the sounding set. CC123
  releases notes (release phase allowed); CC120 silences immediately.
- **Saturation fallback (only when an all-off does not fit).** The host sets
  `VOX_RELEASE_ALL_NOTES_AT_END` / `VOX_RELEASE_ALL_SOUND_AT_END`, clears the
  sounding set and drops that slot's remaining NoteOns for the block. The
  module applies these flags **after** the block's queued events. This is the
  only block-end path.
- Panic, the all-off events and both fallback flags also end sustained voices
  (Rack semantics, stricter than MIDI CC123) and clear `sustainDown`.
- **Guarantee.** After any block, every note a slot owns is either still
  intentionally held (its NoteOff has not arrived) or released; a release
  request is never lost. Only NoteOns and redundant events are dropped, all
  counted. Other channel messages (CC, pitch bend) use free room after the
  reserve and are coalesced to the last value per controller on overflow.
- **Timing, summarized.** Queued NoteOff/CC123/CC120: exact sample offset.
  Rack Panic: start of the next callback (sample 0), whatever the queue holds.
  Saturation fallback: end of the current block, only when an all-off did not
  fit.

### Required tests (slice 3)

- Allocation probe at zero during `process`, plus a Linux CI interposer that
  fails on `malloc`/`pthread_mutex_lock` from the audio thread during routing.
- Overload: more host edits than one per parameter per block; asserts final
  values, counters and no allocation.
- Key semantics in the real test-DLL conformance suite: repeated same-pitch
  NoteOn → single NoteOff ends the key; NoteOff under CC64 then CC64 release;
  voice stealing across keys with every key ending; panic and all-off under
  saturation with sustain down.
- Panic timing: empty queue with an active voice, panic consumed at block
  start and requested mid-callback (takes effect at the next block start), at
  64- and 2048-sample blocks; asserts output silent from sample 0 of the
  panic block, NoteOns later in that block sound, and the saturated fallback
  still ends every key at block end.
- MIDI flood above capacity on **all 16 channels**: NoteOn/NoteOff floods,
  more than C × CC120/CC123, panic mid-flood; asserts zero sounding notes and
  zero active voices in the test module after each release, no stuck notes on
  following blocks, counters, and no allocation/lock.
- Change array ordering/duplicate test; `reset` keeps parameter values.

## Musical transport (in the header since #68; still part of the unfrozen V2)

`samplePosition` alone cannot drive Acid's LFO Sync, STEP modulation or
sequencer (#59). V2 adds one host-owned, versioned transport record per block:

```c
typedef struct VoxTransportV2 {
  uint32_t structSize, version;
  uint32_t flags;               /* VOX_TRANSPORT_* below */
  uint32_t timeSigNumerator;    /* HAS_TIME_SIGNATURE */
  uint32_t timeSigDenominator;
  uint32_t reserved0;
  double tempoBpm;              /* HAS_TEMPO; constant for the block */
  double ppqPosition;           /* HAS_PPQ; at sample 0 of the block */
  double barStartPpq;           /* HAS_BAR_START */
  double loopStartPpq;          /* LOOPING */
  double loopEndPpq;
  int64_t timeInSamples;        /* HAS_TIME; host timeline at sample 0 */
} VoxTransportV2;               /* 72 bytes, no padding */

/* flags */
HAS_TEMPO, HAS_PPQ, HAS_BAR_START, HAS_TIME_SIGNATURE, HAS_TIME,
PLAYING, LOOPING, DISCONTINUITY
```

`VoxProcessContextV2` gains an appended `const VoxTransportV2 *transport`
(host-owned, valid for the call, never NULL; the context grows from 72 to 80
bytes). A module reads it only when the context's `structSize` covers it.

Semantics:

- **Source.** The host fills it once per callback from the DAW play head
  (JUCE `AudioPlayHead::getPosition`, block-start values). A value the host
  does not provide has its `HAS_*` flag clear; it is never invented.
- **No inference.** A module never derives tempo or musical position from
  `samplePosition` or wall-clock time. Without `HAS_TEMPO`/`HAS_PPQ`,
  tempo-synced features stop or hold; each instrument documents which.
- **Within a block.** Position at sample *n* is
  `ppqPosition + n * tempoBpm / (60 * sampleRate)`; tempo is block-rate.
- **DISCONTINUITY** is set on the first block after prepare, reset or state
  load; on stopped→playing and playing→stopped; and when `ppqPosition` or
  `timeInSamples` differs from the previous block's prediction by more than
  half a sample (seek, loop wrap, tempo-map jump). A module resynchronises
  from the new position and must not interpolate across it. A tempo change
  alone is not a discontinuity.
- **Loop wraps inside a block** are not representable with block-start
  values; the wrap appears as DISCONTINUITY on the next block. This is a
  documented V2 limitation shared with the current Rack.
- **Stopped.** `PLAYING` clear; the host still passes the last known
  `ppqPosition`/`timeInSamples` and tempo with their `HAS_*` flags when the DAW
  reports them (most do while stopped). Sequencers do not advance; free-running
  sound continues; tempo-synced LFOs hold their phase at the reported position.
- **Tempo unavailable.** `HAS_TEMPO` clear (for example a host without a play
  head, or the standalone wrapper): synced LFOs and the sequencer stop at the
  last valid phase; nothing falls back to a default BPM or wall clock. When
  tempo returns, DISCONTINUITY is set and phase is recomputed from the ppq.
- **Project reopen.** Transport is never part of module state. After
  `loadState` the first block carries DISCONTINUITY and the module derives its
  musical phase only from that block's ppq, so a reopened project at the same
  position produces the same phase as an uninterrupted run.
- **Block partitions.** Musical phase is a pure function of
  `ppqPosition + n * tempoBpm / (60 * sampleRate)`, so with constant tempo the
  output does not depend on how the host splits blocks. With tempo automation
  the host only reports block-start tempo, so phase is exact at every block
  start and linear within a block; renders with different block sizes can then
  differ by at most one block's tempo slope. This is the V2 limit, stated, not
  hidden.
- **Determinism.** The same host timeline (offline render, same block sizes)
  yields the same transport sequence and therefore the same output.

Test when approved: a transport round trip through the test DLL with a
ppq-derived step counter across play, stop, start, seek, loop wrap, missing
tempo, state reload and block-size changes (64/128/512 at constant tempo must
be bit-identical), asserting step positions, DISCONTINUITY handling and
bit-identical repeat renders. No transport UI is added.

## Threads

| Call | Thread | May allocate / lock / I/O |
| --- | --- | --- |
| `voxModuleEntryV2`, `getInstrument`, `content` queries, `shutdown`, `VoxHostServicesV2.log` | message | yes |
| `create`, `destroy`, `prepare`, `setAllParameters`, `saveState`, `loadState` | controller (message), audio suspended for the slot | yes |
| `process`, `reset` | audio | no |
| `VoxEditorApiV2` | message | yes (not in audio) |

RT functions must not allocate, lock, do I/O or call back into the host except
`VoxRtServicesV2`.

## Lifecycle

### Process-wide module registry

Several Rack VST3 instances live in one DAW process and share each DLL.

- One process-wide `ModuleRegistry` (function-local static, message thread,
  guarded by a mutex for the rare controller-thread access). Key: canonical
  absolute DLL path plus file identity (volume serial + file index).
- `LoadLibrary` + `voxModuleEntryV2` happen **once per DLL per process**; later
  Rack instances and rescans reuse the cached module table and descriptors.
  Entry is never called twice for a loaded DLL.
- Every Rack instance holds a registry lease; every module instance, descriptor
  view and editor holds a module lease. `shutdown` and `FreeLibrary` run only
  when the last lease of the last Rack instance is gone (in practice: when the
  last Rack instance is destroyed). No hot unload.
- **Version selection:** for each packId the newest installed `packVersion`
  whose ABI range includes the host is chosen, deterministically. Once a
  version is loaded it stays the version for **this process**: a newer pack
  installed while the DAW runs is used after the DAW restarts (not by the next
  Rack instance in the same process). Two versions of one packId are never
  loaded side by side.

### Steps

1. **Scan** (message thread, first Rack instance or explicit rescan): read
   manifests, apply the trust policy, verify hashes, load, call entry, deep-copy
   and validate descriptors, register. Failures are reported per pack; nothing
   partial is registered.
2. **Missing / incompatible module**: existing unresolved-slot policy (identity,
   named values, patterns and opaque payload preserved; silence; no
   substitution).
3. **Update**: after restart, open projects use #65 append-only migration or
   stay unresolved.

## Native code trust

A loaded DLL runs inside the DAW process with the user's rights, so a writable
package folder is code execution. Proposed policy (owner confirmation needed,
see open decision 4):

- **Trusted roots only.** Native DLLs load only from
  `%PROGRAMDATA%\Ultima Vox\Instruments` (writable by an elevated installer).
  `%APPDATA%` is scanned for content and declarative UI only, never for DLLs.
- **Signature before load.** Before `LoadLibraryExW` the host verifies the DLL's
  Authenticode signature with `WinVerifyTrust` and checks the signer
  certificate against thumbprints compiled into the Rack (Ultima Vox signer).
  An unsigned or foreign-signed DLL is rejected and reported; the slot stays
  unresolved.
- **Safe load.** Absolute path only, `LoadLibraryExW(path, nullptr,
  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)`; no search
  path, no current directory, no symlinks/junctions outside the root.
- **Developer opt-in.** An explicit per-machine setting (in the
  `%PROGRAMDATA%` root, so it needs elevation) allows unsigned modules; the
  Rack then shows a persistent "unsigned modules enabled" warning. Off by
  default and off in release installers.
- Licensing stays separate and out of scope.

Until this policy is implemented and reviewed, general scanning stays disabled;
only the CMake test module is loaded, and only in tests.

## Instrument replacement and automation

Replacing an occupied slot with a **different** InstrumentId:

1. UI shows a confirmation naming the old and new instrument and stating that
   the slot's automation lanes will be inhibited.
2. On confirm, the slot's `automationInhibited` flag is set (persisted in Rack
   state; needs a Rack schema bump with migration tests).

### While inhibited

- **Authority:** the Rack's named values drive DSP. Host writes to the slot's
  pool endpoints still change the endpoint's normalized value (JUCE/VST3 give
  the plugin no way to refuse them), but the binding does **not** forward them
  to named values or DSP. They are not queued and are never replayed.
- **UI gestures** go through the Rack adapter: they update the named value
  first (DSP follows) and then the endpoint mirror with begin/perform/end
  gestures, so the knob does not jump and a DAW in Touch/Write records them.
- The slot header shows "Automation paused" with a **Rearm** action.
- **Save/reopen:** the flag and named values are saved; on reopen the
  endpoints are set from named values, the flag stays set, and DAW playback of
  existing lanes still does not reach DSP.

### Rearm (explicit, message thread)

1. Each endpoint is set from its current named value (notifying the host), so
   the mirror and the authority agree at the moment of rearm; nothing written
   during the inhibited period is applied retroactively.
2. The flag is cleared; `setAllParameters` re-snapshots the module.
3. From the next block, host writes drive DSP as for any armed slot. If a lane
   in Read mode then plays a point, the value moves to it: that is the user's
   intent of rearm. Smoothing is the instrument's normal smoothing; no special
   ramp is promised.

Missing modules never trigger replacement. Same-InstrumentId upgrades never
inhibit.

**Not safe until proven in a DAW.** Real Cubase 12–15 and REAPER evidence is
required before this ships: lanes in Read/Touch/Latch/Write while inhibited,
rearm during playback and while stopped, save/reopen with the flag set, undo of
replacement, and lane display vs sound. Automated tests cover only the Rack
state machine.

## Parameter capacity (decision needed before freezing the VST3 layout)

Acid needs 80 independent host controls; the pool gives 64 (+8 macros, which
stay independent performance assignments, not shadow copies). Options:

| Pool per slot | Total endpoints | Acid margin |
| --- | --- | --- |
| 64 (current #65) | 1024 | -16 — fails |
| 96 | 1536 | 16 |
| 128 | 2048 | 48 |

Decide from measurement, not intuition: the #65 processor microbenchmark
(construction, state, idle and automated block cost at each size) plus
Cubase/REAPER VST3 parameter enumeration and project load. Parameters beyond
the pool may exist with `HOST_EXPOSED` cleared (UI-only), never as fake host
knobs.

## Performance budget (to measure before optimizing)

1/2/8/16 active modules, empty/idle slots, FX returns and ducking at 44.1/48/96
kHz and 64/128/512-sample blocks: callback p95/p99/worst, allocations (must be
0), peak memory, module creation and project restore time, host parameter
enumeration time. No per-sample O(all host parameters) work.

## Freeze gates

The ABI is called frozen only when all of these hold:

1. Header compiles as C11 and C++20 in CI with the `sizeof`/`offsetof` checks.
2. A real test DLL built by CMake round-trips entry, getInstrument, create,
   prepare, setAllParameters, process, save/load state, destroy and shutdown
   through `LoadLibrary` on Windows x64.
3. The RT tests above pass, including allocation probe and overload/MIDI
   overflow tests.
4. Two Rack instances in one process share one loaded DLL; entry runs once;
   `shutdown` runs once after the last instance.
5. Trust policy decided and implemented for the loader.
6. Architecture Guard review of the header and this document.

## Delivery slices (each with tests, each reviewed)

1. `VoxModuleAbiV2.h`; C11 and C++20 compile targets with layout asserts; a
   test module built as a real shared library; DLL round trip (gates 1–2).
   *Draft:* `libs/instrument/abi/VoxModuleAbiV2.h`, C11 test module
   `tests/abi/TestModuleV2.c`, C++20 harness `module_abi_v2_tests`. Loading
   and validation stay in the harness until slice 2.
2. Process-wide registry, leases, version selection, scan/validate;
   missing/corrupt/conflicting pack tests; two-instance test (gate 4).
3. Index-based dirty parameter publication, snapshot, bounded MIDI release policy, RT
   allocation probe (gate 3).
4. Trust policy: signature check, roots, safe load, developer opt-in (gate 5).
5. Replacement confirmation + automation inhibit/rearm with state round-trip;
   DAW evidence checklist.
6. Declarative `ui.json` renderer for the common widget set.
7. Acid packaged by the Acid agent against the frozen V2 (after capacity
   decision).

## Open decisions for the owner

1. Pool capacity 64 / 96 / 128 (after measurement).
2. Declarative UI only, or also the native editor interface for Acid's 303 view.
3. Install roots and activation (proposed: `%PROGRAMDATA%` for DLLs, new packs
   after DAW restart).
4. Trust policy: confirm signed-only + elevated developer opt-in as proposed,
   or choose another.
5. Musical transport record above (blocker for Acid on V2).

Owner's provisional recommendations (28.10.2026, planning constraints, not
final release decisions): implement and test against 64 now, 128 as preferred
final pool candidate with 96 as fallback pending Cubase/REAPER comparison;
declarative VOX widgets first with the native editor kept as an extension
point (the 303 step sequencer must be truly interactive); installer-owned
versioned `%PROGRAMDATA%` root, per-DAW-process version selection, activation
after DAW restart; production loads only approved signed code via restricted
absolute paths, unsigned test DLLs only under a visibly warned opt-in, and
certificate/key rotation designed before the trust scheme freezes.
`VoxHostServicesV2.log` stays message/controller-thread only; realtime code
reports through return codes or bounded fault flags.
