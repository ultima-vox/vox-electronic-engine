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

The proposal is an append-only ABI V2.3, gated by size like V2.2:
- **Descriptor:** a module may declare up to 8 stereo **auxiliary buses**,
  each `{ kind, target }`:
  - `SEND` with target return 1–4: the module sums each pad's
    `send.N × pad signal` into it;
  - `OUTPUT` with target 2–8: an additional pad output.
- **Process context:** `auxOutputs` (stereo pointers) and `auxBusCount`,
  provided by the host and cleared by the adapter on failed, faulted,
  bypassed or silenced blocks (the same `blockSilenced` rule as #91).
- **Rack:**
  - adds a slot's `SEND` buses into the inputs of their returns. They follow
    the slot's disable/mute/solo and ducking, and are not scaled by slot
    level (like a pre-fader tap); returns keep their tails;
  - never adds aux buses to Main, so nothing is counted twice.
- **`OUTPUT` buses:**
  - phase B1: summed into the slot's main output, so a pad assigned to Out 2
    still sounds;
  - phase B2: the Rack's additional VST3 stereo outputs, with Cubase and
    REAPER bus activation checked (RACK_AUDIO_ROUTING today declares Main
    only). B2 is its own owner decision.
- **Meters:** an optional fixed `padPeaks[8]` output from the module,
  published by the Rack through atomics for the MIXER meters. No locks and
  no UI dependency.
- **Old modules** see an unchanged context and declare no aux buses.

### C. Sample hand-off (hybrid synthesis + WAV)

- **Sources:**
  - factory samples inside the signed `.vins` (`samples/*.wav`, listed with
    their SHA-256 in the manifest);
  - user WAV files, referenced by path, SHA-256 and length.
- **Decoding:** off the audio thread, by the host (JUCE format readers), on a
  bounded loader worker. It produces immutable float buffers at the file's
  native rate, plus the peak (for Normalize) and a waveform overview for the
  UI. Limits: per-sample length and a per-slot memory budget, both to be
  measured and approved.
- **ABI V2.3, an appended module entry:**
  - `setSample(instance, sampleSlot, const VoxSampleV2 *)`, called only off
    the audio thread. `VoxSampleV2 = { frames, channels, sampleRate, data,
    peak, id }`;
  - the module publishes the new pointer to its audio thread atomically at
    the next block;
  - the host frees a replaced buffer only after the Rack's processed-block
    counter has advanced past the swap (no free while the audio thread may
    read it).
- **Playback (module):**
  - start/end points, One Shot or Loop, and Reverse are read modes of the
    same buffer;
  - interpolated resampling for Tune/Fine and for the file rate;
  - gain and Normalize (`1 / peak`);
  - retrigger with a 3 ms fade, as the existing voices do;
  - per-pad source: Synth, Sample, or Layer (both).
- **State:**
  - the slot saves sample references in the Rack slot state (a new optional
    field, append-only);
  - a missing or changed file (hash mismatch) leaves the pad on its synthesis
    source, reports it in `SlotDiagnostics.lastError`, and offers Locate in
    the UI. The project still opens.

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
