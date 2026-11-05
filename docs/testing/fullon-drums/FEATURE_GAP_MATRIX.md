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
