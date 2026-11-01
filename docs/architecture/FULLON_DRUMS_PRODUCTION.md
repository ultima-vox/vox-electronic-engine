# Full-on Drums production instrument — design (PROPOSED)

Status: PROPOSED. The owner's production task of 31 Oct 2026 replaces the
earlier "Electronic Drums" plan. Owner decisions:
- extend **Full-on Drums** itself (styles psy, full-on, goa, progressive,
  hardcore and more become factory kits and patterns);
- review the contracts for per-pad buses (B) and sample hand-off (C) first,
  then the DSP;
- reconstruct the UI from the MagicPath project `467221166344531968` once the
  owner connects it.

Every phase is its own Draft PR with tests and exact-head Windows CI. Each
needs owner approval before it enters `integration/rack-1.0`. CI green is not
DAW acceptance.

## What stays

- The instrument `com.ultimavox.fullon-drums`: its id, the 59 existing
  parameters with their pool indices 0–58, its synthesis DSP and sound, and
  state version 3 (read by an append-only upgrade).
- The 8 pads: Kick 36, Snare 38, Clap 39, Closed Hat 42, Open Hat 46,
  Tom/Perc 45, Rim/FX 37, Cymbal 49.
- The Rack-owned 8×16×4 pattern (#74), MIDI export (#85), same-block ducking
  and the routed-notes policy (#86, #91), the FX returns (4), trusted signed
  `.vins` loading, and per-slot diagnostics (#90).
- One Rack, one VST3. No second graph, no separate Drums plug-in.

## Responsibilities

| Rack | Full-on Drums module |
| --- | --- |
| loading and lifecycle, shared UI shell and components, MIDI/audio routing, transport, FX returns, sends from the module's send buses, ducking, host automation, project state, slots, macros, master | synthesis and sample playback, per-pad envelope, filter, transient, pitch, level, pan, mute, solo, choke, per-pad sends summed into its send buses, voices, note map, velocity zones, kits and presets, its DSP state |

## Contracts to review first

### A. UI-only parameters — Draft #95

Pad settings that are saved and edited, but not automated: sample choice,
start/end, mode, reverse, normalize, filter type, envelope shape, choke group,
note map, zones, lane solo and so on. Up to 256 parameters per instrument;
at most 128 sit on host endpoints.

### B. Module auxiliary buses (per-pad sends and outputs)

The problem: a slot has one stereo output, and the Rack's sends act per slot.
Per-pad Send 1/2 and per-pad output assignment therefore cannot reach a Rack
FX return today.

**ABI V2.3** (append-only, gated by size like V2.2):
- **Descriptor:** a module may declare stereo **auxiliary buses**, each
  `{ kind, target }`, bounded so that the full advertised topology fits at
  once:
  - up to **4** `SEND` buses, one per Rack return 1–4 (no two with the same
    target);
  - up to **7** `OUTPUT` buses, one per host output 2–8 (no two with the
    same target);
  - so at most **11** buses. Descriptor validation rejects more, duplicates,
    or targets out of range, and the module is not loaded (fail closed).
- **Full-on Drums declares 9:** `SEND` → returns 1 and 2 (pad Send 1, Send 2)
  and `OUTPUT` → Out 2 … Out 8. Every pad output and send option shown in the
  editor has its own bus, so all seven outputs and both sends can be active at
  the same time. The editor offers only the targets the descriptor declared;
  there are no phantom options.
- **Memory:** the Rack allocates aux buffers at `prepare`, only for the buses
  a slot's module declares: `2 × maxBlock × 4 bytes` per bus (Drums at a
  2048-sample block: 9 × 16 KiB ≈ 147 KiB per slot). This is measured and
  reported with the performance phase; nothing is allocated in the callback.
- **Process context:** `auxOutputs` (stereo pointers) and `auxBusCount`,
  provided by the host. The adapter clears all aux buses on failed, faulted,
  bypassed or silenced blocks (the same `blockSilenced` rule as #91).
- **Old modules** see an unchanged context and declare no aux buses.

**Module rule:**
- Every pad is rendered into **exactly one** dry destination: Main, or the
  OUTPUT bus its `pad.output` selects. Never both.
- Independently, the module adds `send.N × pad signal` (taken after pad level
  and pan) into SEND bus N.

**Rack mixing policy, disjoint by bus kind:**

| Signal | B1 (now: the VST3 exposes Main only) | B2 (host outputs active) |
| --- | --- | --- |
| Main | slot dry input | slot dry input |
| OUTPUT bus k | **added once** into the slot dry input, next to Main | goes to host output k through the same slot chain (below), **not** into Main. If output k is inactive in the DAW, it falls back to the slot dry input, once, exactly as in B1 |
| SEND bus N | only into the input of Rack return N, never into Main or any OUTPUT | same |

**Slot chain** for the slot dry input and for each B2 output path: ducking,
then slot level/pan, gated by disable/mute/solo. The slot's own pre/post sends
tap the **sum of Main and every OUTPUT bus**, in both B1 and B2. A pad
therefore keeps the same dry gain, ducking and slot-send contribution when
the project moves from B1 to B2; only the physical destination changes.

SEND buses follow the slot's disable/mute/solo and ducking, and are not
scaled by slot level (like a pre-fader tap). Returns keep their tails, as
today.

**State:** `pad.output` (Main / Out 2 … Out 8) is a UI-only parameter, saved
now. Under B1 it changes nothing audible. After B2 is enabled, the same
project routes the pad to the host output as soon as that output is active.

**Meters:** an optional fixed `padPeaks[8]` output from the module, published
by the Rack through atomics for the MIXER meters. No locks and no UI
dependency.

**Regression tests:**
- maximum topology: seven pads on Out 2 … Out 8 while every pad sends to
  both Send 1 and Send 2, all at once; every signal is present exactly once
  where the policy puts it, and none is aliased or dropped;
- descriptors with 5 SEND or 8 OUTPUT buses, duplicate targets or
  out-of-range targets are rejected deterministically;
- a pad routed to Out 2 under B1 sounds exactly once (bit-equal to the pad on
  Main);
- simultaneous Main + Send + Out 2: each signal appears only where the table
  puts it;
- the B1→B2 switch keeps dry gain, ducking and slot sends equal; an inactive
  B2 output falls back exactly once;
- ducking does not depend on slot order;
- send-return tails survive mute/solo; faulted or silenced modules leave
  every aux bus silent (no phantom send or duck).

### C. Sample hand-off (hybrid synthesis + WAV)

**Sources:**
- factory samples inside the signed `.vins` (`samples/*.wav`, listed with
  their SHA-256 in the manifest);
- user WAV files, referenced by path, SHA-256 and length.

**Decoding:** off the audio thread, by the host (JUCE format readers), on a
bounded loader worker. It produces immutable float buffers at the file's
native rate, plus the peak (for Normalize) and a waveform overview for the UI.
Limits: per-sample length and a per-slot memory budget, both to be measured
and approved.

**Ownership and lifetime.** The host owns every decoded buffer and frees it
only through the retirement protocol below. The module never allocates or
frees sample memory.

**Amendment (C rev. 2, for review).** The first revision put the
cross-thread protocol (generations, voice counts, epoch, `sampleInUse`) inside
the module. Native modules are C11 built with MSVC, where `<stdatomic.h>` is
only available behind `/experimental:c11atomics`; no shipped module uses C
atomics today. Revision 2 keeps every cross-thread step in the C++ host and
gives the module a single-threaded contract.

- **ABI V2.3, appended, size-gated:**
  - the instrument descriptor declares `sampleSlotCount` (0..64);
  - the process context carries `samples`, a host-owned, read-only table of
    `sampleSlotCount` entries for this call. Each entry is
    `VoxSampleV2 { structSize, version, id, channels, channelCount, frames,
    sampleRate, peak }`, with `id = 0` meaning no sample;
  - the process context also carries `sampleUsage`, a host-owned buffer of
    `uint64_t` ids with a capacity. Before returning, the module writes the
    ids of every buffer any of its voices still references (deduplicated) and
    sets `overflow` if they do not fit.
  - The module may read a buffer only during `process`, and only one listed
    in the current call's table or held by a voice it started on that buffer.
    It never allocates, frees or caches sample memory.
- **Usage report (module → host), validated per block.** The module writes
  `sampleUsage = { count, overflow, ids[capacity] }`. The adapter marks the
  block's report **valid** only when all of these hold:
  - `process` returned `VOX_OK` and the block really ran (not faulted,
    suspended or unprepared; a bypassed block runs the module and counts);
  - `count ≤ capacity`;
  - every id is non-zero and appears once;
  - every id is one the host gave this instance and has not freed.

  Anything else (no call, an error, a malformed report) yields **no report**
  for that block. `overflow` yields a report that frees nothing.
  Missing reports never read as "empty".
- **Host (adapter, C++), data-race-free publication.**
  - **Pending → current:** the controller writes a pending entry per sample
    slot (an atomic pointer to an immutable host object). At block start the
    audio thread takes it with one atomic exchange and installs it in its
    private table.
  - **Report:** after `process`, the audio thread fills a **report buffer it
    alone owns**:

    ```
    { epoch N, valid, overflow,
      current ids of every sample slot in block N,
      usage ids }
    ```

    It then hands the buffer over through a single-writer/single-reader
    **triple buffer**: three fixed report buffers, each owned exclusively by
    the writer, the middle slot or the reader. Ownership changes only by an
    atomic exchange of a small index word with acq_rel order, plus a "fresh"
    bit. The writer never touches the reader's buffer, and neither side
    waits, locks or allocates. The reader may see only the newest report, and
    skipping one is safe because each report is self-contained.
- **Free rule (controller).** A buffer X that the controller replaced or
  cleared is freed only after it reads a report that meets all of these:
  - it is coherent (taken by the exchange above);
  - it is valid and non-overflowing;
  - its epoch is strictly later than the epoch in which the controller saw
    X's replacement applied;
  - X is in neither its current ids nor its usage ids.

  From that block on no voice holds X, and none can start on it, because X
  is in no table and the controller never re-installs a retired buffer.
- **Verified quiescence** is the only other path that frees buffers:
  - `prepare` and `reset` that return `VOX_OK` end every voice and drop
    every sample reference (module contract), and the Rack calls them inside
    its exclusive section, with no `process` running concurrently;
  - Panic and transport-stop reset run `reset` on the audio thread at a block
    start; the adapter then publishes that block's report with `valid` set
    only if `reset` returned `VOX_OK`;
  - instance destroy (slot unload, project reload, shutdown) happens off the
    audio thread after the Rack's exclusive section, and only then are its
    buffers freed;
  - a faulted instance frees nothing until a successful `prepare` (recovery)
    or destroy.
- **Bounds:** at most 4 retiring buffers per sample slot; `setSample` refuses
  when they are all busy, and the UI retries after the next poll. Usage
  capacity is `sampleSlotCount × 4`. Report buffers are preallocated at
  `prepare`. There is no unbounded queue and no allocation, free, lock, wait
  or I/O in the callback.

**Playback (module):**
- start/end points, One Shot or Loop, and Reverse are read modes of the same
  buffer;
- interpolated resampling for Tune/Fine and for the file rate;
- gain and Normalize (`1 / peak`);
- retrigger with a 3 ms fade, as the existing voices do;
- per-pad source: Synth, Sample, or Layer (both).

**State:**
- the slot saves sample references in the Rack slot state (a new optional
  field, append-only);
- a missing or changed file (hash mismatch) leaves the pad on its synthesis
  source, reports it in `SlotDiagnostics.lastError`, and offers Locate in the
  UI. The project still opens.

**Stress tests** (ASan on Linux, plus Windows CI stress; the allocation, lock
and free probes stay at 0 in the callback):
- replace during a long one-shot and during a loop;
- rapid A→B→C swaps;
- retrigger and fade across the swap;
- slot unload, stop, Panic and project reload while voices ring;
- instance destroy/shutdown with retiring buffers;
- the retirement list full (refused, then accepted);
- a deterministic oracle: output with a swap equals output without it until
  the new voices start;
- a usage report that overflows frees nothing;
- a **logical-retirement canary**: the test release hook poisons a buffer with
  NaN and keeps its memory quarantined (never deallocated during the test).
  Every render must stay finite, and the test asserts when each buffer was
  released. Real deallocation is checked separately under ASan;
- **fault injection:**
  - process failure right after A→B;
  - bypassed, suspended and unprepared blocks;
  - an invalid count, a zero id, a duplicate id, an unknown id;
  - overflow;
  - stop and Panic;
  - slot unload and project reload.

  In each case nothing is released early, and the callback still has no
  alloc, lock or free;
- a **concurrent producer/consumer stress test** of the report triple
  buffer, run under ThreadSanitizer on Linux: rapid blocks, a deliberately
  delayed reader, forced overflows. A live id is never freed.

### D. Pattern content V2.1 (per-step probability)

Per-step probability (0–100 %) is appended to the pattern step, and the
pattern gets a seed.
- Probability and Humanize use a counter-based RNG keyed by the seed, bar,
  lane and step. Offline renders and MIDI export are therefore reproducible.
- Old patterns read as 100 %.

### E. Macros on direct instruments

Today a direct instrument rejects macro mapping. The proposal: each of the 8
slot macros may target up to 8 parameters, each with range and direction.
The assignments are saved with the slot; a macro is a host parameter;
modulation evaluates at block boundaries, as the existing matrix does.

## Host parameters (pool)

Existing: 0–58. Appended, so earlier automation stays where it is:

| Pool | Parameters |
| --- | --- |
| 59–98 | per pad: Send 1, Send 2, Cutoff, Resonance, Fine (5 × 8) |
| 99–100 | Seq Humanize, Seq Probability |

Total 101 of 128. Everything else is UI-only.

## Features by tab (each control: UI → parameter/command → contract → DSP/state → test)

| Tab | Content |
| --- | --- |
| SOUND | pad bank (select, audition, mute/solo, power), source (synth / sample / layer), sample browser, waveform with start/end, One Shot/Loop/Reverse/Normalize, ADSR editor, filter (type LP12/24, HP12/24, BP; cutoff, resonance; curve computed from the same coefficients), transient (attack/sustain), pitch (tune/fine), performance (pan, level, Send 1/2, output) |
| PATTERN | existing grid and modes, plus per-step probability, Humanize, seed, Fill, Shift, Invert, Copy/Paste, Clear, pattern import/export, lane mute/solo |
| MIXER | 8 pad strips (volume, pan, mute, solo, sends, output, meter) and the kit master |
| ROUTING | note map (editable, with validation), MIDI channel, Host/Internal/Both, choke groups, pad outputs, send targets, ducking source |
| ZONES | per-pad note zones, velocity ranges and response curve |
| MACROS | 8 macros, multi-target with ranges (contract E) |
| ADVANCED | voices and stealing, retrigger policy, choke handling, MIDI Learn, note-map and preset management, parameter and instrument reset, diagnostics (#90), module and state versions |

## Phases

| # | Scope | Gate |
| --- | --- | --- |
| A | UI-only parameters | #95 review |
| B | ABI V2.3 aux buses: Rack sends from pads, meters (B1); host outputs (B2) | architecture review |
| C | sample hand-off, loader, state references, factory samples | architecture review |
| D | pattern content V2.1 probability/seed | review |
| E | macros on direct instruments | review |
| 1 | DSP: per-pad ADSR, filter, transient, fine, sample player, layers, choke groups, zones, note map; kit parity tests against the current sound | after A–C |
| 2 | sequencer tools and pattern I/O | after D |
| 3 | VOX component library and the seven tabs from the MagicPath references; visual comparison harness at reference size | references connected |
| 4 | factory kits/styles and presets; signed `.vins` with samples | after 1 |
| 5 | performance measurements (Drums alone; Drums + Bass + Acid + FX); limits proposed for owner approval | after 1–4 |
| 6 | release candidate: exact-head Windows CI, Cubase/REAPER acceptance by the owner | owner |

Realtime rules for every phase: no allocation, lock, I/O, decoding, logging
or UI dependency in the audio callback; bounded queues; validated ranges;
predictable bypass; no stuck notes.
