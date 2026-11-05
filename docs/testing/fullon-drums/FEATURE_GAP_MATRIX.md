# Full-on Drums 1.0: feature-gap matrix (actual code)

Baseline: `integration/rack-1.0` **f9c8bc6** (#78). Release scope: Rack +
Full-on Drums 1.0 (#77), with no Bass or Acid.

I traced every item to code, test, UI and endpoint. The design documents
(#94) were not counted as evidence.

States:
- **WORKING+TEST**: code, an automated test and a UI control exist.
- **BACKEND ONLY / UI MISSING**: the DSP or state works and is tested, but
  there is no editor control.
- **MISSING**: no code.
- **PROPOSE OWNER DEFERRAL**: I propose leaving it out of 1.0; this needs the
  owner's approval in #77.

Main paths:
- module: `libs/drums/VoxFullOnDrums.c`;
- SOUND layout: `instruments/fullon-drums/sound.json`;
- editor: `apps/vst3/PluginEditor.cpp`, `libs/ui/PatternGrid.cpp`;
- tests: `tests/integration/FullOnDrumsTests.cpp` (function names below).

Parameters: 187 descriptors.
- Host-exposed pool **0..98**: 99 parameters, `descriptor index == pool index`.
- UI-only **99..186**: 88 parameters, no endpoint.
- Host pool maximum per slot: 128.

## SOUND / per-pad DSP

| Feature | State | Evidence |
|---|---|---|
| 8 pads, fixed GM notes 36/38/39/42/46/45/37/49 | WORKING+TEST | `defaultNotes`; `grooveAndStems`, `parametersAffectOnlyTheirPad` |
| Level, Tune, Decay, Pan, Character per pad (pool 0..39) | WORKING+TEST | `PAD()`; SOUND layout; `parametersAffectOnlyTheirPad`, `kitParity` |
| Kick specials: Pitch Env, Pitch Decay, Drive, Sub, Tail (41..45) | WORKING+TEST | `G_KICK_*`; `kitParity` |
| Fine ±100 ct, Cutoff, Resonance, Send 1/2 per pad (59..98) | WORKING+TEST | `PAD2()`; `phase1Shaping`, `phase1BusesAndSends` |
| Filter type Off/LP12/LP24/HP12/HP24/BP, Transient Attack/Sustain, Output Main/Out 2–8 (UI-only) | WORKING+TEST | `phase1Shaping`; `editorSoundLayout`; `hostOutputsB2` |
| Synth / Sample / Layer, Start, End, Loop, Reverse, Normalize, Sample Level (UI-only, 131..186) | **BACKEND ONLY / UI MISSING** for choosing a sample | `sampleSources`, `sampleReverseInterpolation`, `sampleVoicesInTheKit`. The knobs exist in the SOUND layout, but the editor has **no sample browser, drop target or file assignment**; `assignSampleFile` is only called from code and tests |
| WAV waveform with start/end markers | **MISSING** | no waveform component |
| Full amplitude envelope (attack / hold / decay / release) | **MISSING** | one-shot `Decay` only; the transient shaper is not an envelope |
| Velocity response | WORKING+TEST (global) | `G_VELOCITY` (pool 47) is global; there is no per-pad curve |
| Velocity zones / layers per pad | **MISSING** | — |
| Choke groups | **BACKEND ONLY / UI MISSING** (partial) | `choke[]` per pad in state, `defaultChoke` closed/open hat; `Hat Choke` on/off (46); `chokeAndRelease`. Groups are **not editable** |
| Note map (pad note) | **BACKEND ONLY / UI MISSING** | `padNote[]` is saved in module state (`saveState`/`loadState`); there is no parameter or editor to change it |
| Per-pad mute / solo | **MISSING** | `seq.laneN` (51..58) mutes only the internal sequencer lane, not the pad |
| Pad audition | WORKING (generic) | the editor keyboard sends notes to the selected slot; there are no pad buttons |
| Voice policy (16 voices, retrigger fade, choke fade) | WORKING+TEST, fixed | `MAX_VOICES 16`; `chokeAndRelease`, `sampleOffsetsAndSimultaneousHits`; not configurable |

## PATTERN

| Feature | State | Evidence |
|---|---|---|
| Rack-owned 8 lanes × 16 steps × up to 4 bars, velocity and accent | WORKING+TEST | `PatternGrid` (step toggle, shift accent, drag velocity, bar view, steps/bars/timing boxes); `editorDrumGrid`, `sequencerPatternEdits` |
| 1 / 2 / 4 bars, steps per bar, timing | WORKING+TEST | `pattern::reshape`; `editorDrumGrid` |
| Swing, Accent amount, lane on/off (48..58) | WORKING+TEST | `sequencerFollowsLoopsSeeksAndSwing`, `exportMatchesPlayback` |
| Host / Internal / Both with no doubled hits | WORKING+TEST | `sequencerModesAvoidDoubleTriggers`, `routedNotesReport`, `strayKickPolicy` |
| Loop / seek / stop follow the host grid | WORKING+TEST | `sequencerPlaysOnTheHostGrid`, `sequencerFollowsLoopsSeeksAndSwing` |
| MIDI export of the audible stream (NoteOn/NoteOff, offset, channel), parity under block partitions | WORKING+TEST | `exportMatchesPlayback`, `exportEndsAfterTheLastNote`, `multiTrackHostMidi`; editor export command |
| Save / reopen without duplicates | WORKING+TEST | `sequencerStateReopensWithoutDuplicates`, `projectsRestore` |
| 5 factory patterns (Full-on Basic/Driving, Rolling Offbeat, Breakdown, Kick & Offbeat) | WORKING+TEST | `patternNames`; `factoryPatternsAndInit` |
| Probability + seed, Humanize | **MISSING** | pool 99..100 is reserved (#100). The Rack seed (`rngSeed`) exists but Drums does not use it |
| Step actions: Fill / Shift / Invert / Copy / Paste / Clear | **MISSING** | `PatternGrid` has no such commands |
| User pattern save / load (outside the project) | **MISSING** | the pattern lives only in the project state |

## MIXER / ROUTING / ZONES / MACROS / ADVANCED

The editor has **6 generic Rack pages**: SOUND, PATTERN, ROUTING, ZONES,
MACROS, ADVANCED. There is no Drums-specific seven-tab UI and **no MIXER
page**.

| Feature | State | Evidence |
|---|---|---|
| Per-pad output Main/Out 2–8 and sends 1/2 | WORKING+TEST (SOUND knobs/choices) | `hostOutputsB2`, `phase1BusesAndSends`; there is no mixer strip |
| Eight pad strips with real meters | **MISSING** | `padPeaks` is in the design (#94 contract B, meters) but not implemented |
| Slot routing: channel, key/velocity zone, transpose | WORKING+TEST (Rack) | `RoutingPage`, `ZonesPage` (`ZoneRangeEditor`); `rack_router_tests`, `routing_equivalence_tests`. These are **slot** zones, not pad zones |
| Kick ducking of another slot, Rack sends/returns, B2 | WORKING+TEST | `RackRoutingPanel`; `internalKickDucksInTheSameBlock`, `sampledKickDucksInTheSameBlock`, `hostOutputsB2` |
| Macros (8 per slot, mapped to instrument parameters) | WORKING+TEST (Rack) | `MacrosPage`, `macroAssignments`; Rack macro tests. The Drums module defines **no macro targets** of its own |
| MIDI Learn | **MISSING** | — (propose deferral) |
| Diagnostics (slot errors, dropped MIDI, sample errors, counters) | BACKEND ONLY / UI partial | `SlotDiagnostics` (`sampleError`, dropped MIDI, overflow); the editor does not show all of them |
| ADVANCED page | WORKING (Rack) | MIDI source mode, seed, global FX preset; nothing Drums-specific |

## Content

| Feature | State | Evidence |
|---|---|---|
| Factory patterns | WORKING+TEST (5) | above |
| Factory kits / sound presets for Drums | **MISSING** | the processor has `applySelectedSoundPreset`, but the Drums `.vins` declares none |
| Factory WAV samples in the `.vins` (with SHA-256) | **MISSING (no content)** | the loader supports `descriptor.factorySamples` (#99). The Drums package has **no samples**, and no licence inventory exists |
| User WAV assignment, save and restore, missing file | WORKING+TEST (backend) | `loaderAssignsKitSamples`, `savedSamplesNeverDropped`; a missing file keeps the synthesis and the slot diagnostic |
| Locate / replace a missing file (UI), hash-mismatch warning | **BACKEND ONLY / UI MISSING** | the error text exists in `sampleError`; there is no Locate UI |
| User kit save / load | **MISSING** | — |

## Realtime / reproducibility

| Feature | State | Evidence |
|---|---|---|
| Deterministic render, save → reopen identical | WORKING+TEST | `deterministicOfflineRender`, `projectsRestore`, `loaderAssignsKitSamples` |
| Failure paths silence Main and every aux bus | WORKING+TEST | adapter `blockSilenced`; `native_rack_tests` |
| Sample lifetime under rapid swaps | WORKING+TEST | `sampleLifetimeInTheKit`, `realtimeAllocationsWithSampleSwaps`, `sample_exchange_tests` |
| Panic / Stop / CC120 / CC123 / NoteOff / tails / choke | WORKING+TEST | `chokeAndRelease`, `sampleVoicesInTheKit`, Rack lifecycle tests |
| 0 callback allocations (Linux and Windows probe) | WORKING+TEST | `allocation_probe_tests`, `noAllocationWhilePlaying`, `realtimeAllocationsWithSampleSwaps` |
| Lock/IO probe on Windows | **MISSING** | allocation only; locks are covered by the Linux `--wrap` gate |
| CPU headroom 44.1/48/96 × 64/128/512 | **P1 OPEN** | #106/#107: 96 kHz/64 at the declared limit misses deadlines (21–28/6000 on the shared runner) |

## Missing owner input
1. **Visual reference.** MagicPath reference `467221166344531968` is not
   available in this environment: MagicPath access was refused earlier, so I
   did not work around it. A seven-tab UI needs an owner-supplied export or
   screenshots. Without one, I will propose a traceable component/layout
   acceptance and not invent visual acceptance.
2. **Factory sample content and licence.** No bundled WAVs exist. The owner
   needs to choose the sources and licences, or approve synthesized-only
   factory kits for 1.0.
3. **Production signing.** The RSA-3072 public key to pin, and the offline
   signature, come from the owner. The private key never enters the repo, CI
   or chat.
4. **Deferral decisions** (#77): MIDI Learn, per-pad velocity layers,
   user-configurable voice count.

## GENRE-NEUTRAL SOUND DESIGN (VOX Drums, owner correction 6650325551)

The instrument is one genre-neutral drum machine. Full-on, Psy, Goa,
House/Techno and Hip-hop/Breaks are kit, sound and pattern presets.

### Where synthesis is tied to the pad role (f9c8bc6)

| Location in `libs/drums/VoxFullOnDrums.c` | What is bound to the pad index |
|---|---|
| `trigger()` → `switch (pad)` (≈ l.563) | base pitch per role: kick 50 Hz, snare 185, clap 1200 (scaled by character), tom 120, rim 520, hats 1.7× ratio, cymbal 3.1×; kick-only latching of `G_KICK_*` |
| `renderVoice()` → `switch (v->pad)` (≈ l.710) | one fixed algorithm per pad. Kick: KickSynth port (swept sine + sub + click, tanh drive). Snare: 2 sines + HP noise. Clap: noise bursts through BP. Hats/cymbal: metallic partials + noise through HP (cutoff differs by `v->pad == PAD_CYMBAL`). Tom: swept sine + click. Rim: 2 sines + HP click |
| `parameters[]`: `PAD("kick", …, "Click")`, `"Snappy"`, … and `G_KICK_PITCH/PITCH_DECAY/DRIVE/SUB/TAIL` (41..45) | the meaning of `character` per role; five kick-only global parameters |
| `defaultNotes`, `defaultChoke` | GM note map and hat choke (both stored in state, see above) |

### What already varies without new DSP

| Means | State | Evidence |
|---|---|---|
| Per-pad Tune ±12 st, Fine ±100 ct, Decay, Level, Pan, Character, Filter type/Cutoff/Resonance, Transient Attack/Sustain, Sends, Output | WORKING+TEST | see SOUND above |
| **Sample** source per pad (any WAV, start/end/loop/reverse/normalize/level) | WORKING+TEST (backend); UI missing | `sampleSources`, `sampleReverseInterpolation`, `loaderAssignsKitSamples` |
| **Layer**: the pad's synth algorithm plus a sample | WORKING+TEST | `sampleSources`, `sampledKickDucksInTheSameBlock` |
| Note map / choke groups (stored in state) | BACKEND ONLY / UI MISSING | `loadState` validation |

So a sample-based kit of any style is possible today through the backend. It
is **not** true that every pad can synthesise every timbre: a pad can run only
its own algorithm, and the kick specials belong to the kick pad.

### Proposal: per-pad synth model (PROPOSE, owner decision for v1.0)

Make the algorithm a per-pad choice. Keep the eight verified algorithms
exactly as they are; this is not a DSP rewrite.

- **Descriptors.** Append 8 UI-only CHOICE parameters `<pad>.model`, with
  choices Kick, Snare, Clap, Closed Hat, Open Hat, Tom, Rim, Cymbal. Each
  pad's **default is its own role**.
  - Host pool unchanged: 0..98.
  - UI-only becomes 99..194; the descriptor count goes 187 → 195.
  - `instrumentVersion` 3 → 4, with the Rack's append-only upgrade.
- **DSP.** `trigger()` and `renderVoice()` switch on `v->model` instead of
  `v->pad`.
  - The hat/cymbal cutoff test becomes `model == Cymbal`.
  - The kick specials (41..45) apply to every pad whose model is Kick. They
    are shared across those pads, a documented limitation. Per-pad kick
    parameters can be appended later if needed.
- **State.** Module state v5 → v6. v5 still loads, with model = role.
- **Bit-exact at defaults.** With every model left at its role, the
  arithmetic is unchanged, so `kitParity` (1e-6 against the 1.x fixture) and
  all existing tests must stay green. Rollback means setting the models back
  to their roles.
- **Tests:**
  - parity at defaults;
  - a pad running another model equals that model's pad at the same
    tune/decay/character (bit-equal for the same latched values);
  - state v5 → v6 upgrade;
  - choke and note map unchanged;
  - same-block kick ducking from a non-kick pad running the Kick model (the
    duck note is the pad's note);
  - allocation probe.
- **CPU.** The cost per voice is the cost of the chosen algorithm, so the
  worst case is 16 voices all running Kick. That is bounded by
  `MAX_VOICES 16` and is measured by `callback_headroom_bench`; I will add a
  "16 voices, all Kick model" case at 96 kHz / 64.
- **Recommendation.** Include it in 1.0. It is a small change, bit-exact at
  defaults, and it gives real genre neutrality for synthesised kits. The
  owner decides.

### Kits that sound different (owner item 3)

At least three distinct kits/patterns with real sound need both of these:
- **Synth-only kits**, possible now with existing DSP plus the model choice.
  I can render audio evidence: Full-on/Psy, House/Techno, Hip-hop/Breaks
  presets of real parameter values and patterns, each with rendered WAVs and
  a test that they differ.
- **Licensed samples**: no redistributable WAVs exist in the repo. The owner
  must supply or approve sources. Each shipped sample needs a licence, a
  source, attribution and a SHA-256 in the package manifest.

## Pre-release rename impact: "Full-on Drums" → **VOX Drums** (analysis only, no rename)

| Identifier | Where | Effect of renaming |
|---|---|---|
| InstrumentId `com.ultimavox.fullon-drums` | `DRUMS_ID` in the module; `vox_add_vins … ID / INSTRUMENTS / UI` in `CMakeLists.txt`; tests (`drumsId`) | **project state**: saved slots store the InstrumentId, and `directKey(instrumentId, id)` hashes it into the direct-parameter keys. Every saved project and fixture with the old id stops resolving. This is acceptable **only before 1.0**, because there are no user projects (owner confirmation in AGENTS.md). Host endpoint ids `slotNN.pKKK` do **not** change |
| Package `VoxFullOnDrums.vins` and its manifest id / signature | `CMakeLists.txt` l.737–743, CI install layout, `build-info.txt` | repackage and re-sign. The signature covers the manifest, so it must be the production signature after the rename |
| Module target / DLL `VoxFullOnDrums` | `CMakeLists.txt` l.723–735, `.vins` `MODULE_TARGET` | file name only, no ABI effect |
| Source paths `libs/drums/VoxFullOnDrums.c`, `instruments/fullon-drums/`, `docs/testing/fullon-drums/`, `tests/integration/FullOnDrumsTests.cpp` | CMake, workflow (5 lines), tests | paths only |
| Display name "Full-on Drums" | `.vins NAME`, the descriptor name, editor and DAW display | user-visible only |
| Cache directory `cache/com.ultimavox.fullon-drums/…` | vins loader | derived from the id; a new cache |
| Fixtures `tests/fixtures/drums/kit-parity-v1.txt` | kit parity | content does not depend on the id; it stays |

**Recommended stable IDs:**
- `com.ultimavox.drums`;
- package `VoxDrums.vins`, module `VoxDrums`;
- display name **VOX Drums**.

The rename should be done once, in one reviewed PR, **before** the RC and
before production signing, with no alias and no dual loader. Its acceptance
is a full test pass with the new id, plus a check that no `fullon-drums`
identifier remains outside the kit/preset names.
