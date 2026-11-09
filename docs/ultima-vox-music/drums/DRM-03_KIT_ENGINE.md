# DRM-03a: the VOX Drums kit engine (16 pads, 8 models)

Status: on `drm/03-kit`, stacked on DRM-02. This is the **engine slice** of
DRM-03. The plugin still plays the wrapped legacy module.

DRM-03b, the next PR, switches the processor to this engine and removes the
legacy copy. It also brings the parameter map, state and editor for 16
pads and 8 models.

## What it is

`products/drums/engine/KitEngine.{h,cpp}` (`vox::drums::kit`) is a C++
engine with no legacy ABI. Its synthesis is a line-by-line port of the
reviewed module (`VoxFullOnDrums.c` @ f9c8bc6), generalised.

| | Legacy module | Kit engine |
|---|---|---|
| Pads | 8, fixed types | **16**; any pad plays any of **8 models**: Kick, Snare, Clap, Hat, Tom, Rim, Cymbal and Perc (new) |
| Notes | fixed 36/38/39/42/46/45/37/49 | any note per pad; the lowest pad wins a shared note |
| Voices | 16, one per pad (a retrigger fades the previous hit) | **64**, up to **8 per pad** (polyphony 1–8 per pad); oldest stolen |
| Choke | hats only, fixed group | any pads, any group; on a coincident hit the lowest pad of the group wins (the legacy rule: closed hat cuts open hat) |
| Kick body (pitch env, drive, sub, tail) | kit-wide | per pad (Kick model) |
| Samples | 1 player per pad | **2 players (A, B)** per pad: *A only*, *velocity switch* at a split, or *mix*; Sample / Layer source as before |
| Outputs | main + 2 sends + Out 2..8 | same, unchanged |
| Block size / MIDI | ≤ 8192 frames, ≤ 512 events per call | any block size, any event count (no chunking at the plugin) |
| Sequencer | 8 lanes, 16 steps × 4 bars | 16 lanes (patterns as before; editing is DRM-04) |

**Perc** is the only new sound. It is a bounded two-operator FM blip plus a
band-passed noise burst. Character moves from a pure tone to a bright,
noisy zap; the FM index decays and is capped.

**Realtime:** `process()` allocates nothing, takes no lock and does no I/O;
every loop is bounded. Settings are plain structs, copied by the caller
between blocks.

**Settings are sanitised** (review 6737829467). At each block start the
engine copies the settings into a fixed-size member and brings every field
into range:
- In-range values stay bit-identical, so the parity results do not change.
- NaN takes the default; infinities and other out-of-range numbers are
  clamped to the nearest bound.
- An enum outside its set takes its first value.
- An output bus outside 0..7 plays on **Main**.
- A sample region is clamped to 0..1.

The aux index is also bounds-checked at the write. The original defect was
that `output = 8` wrote past the stack bus array: ASan reported a
stack-buffer-overflow.

## Evidence (local, Linux GCC 13 Release): 26/26 suite tests pass

| Test | Proves |
|---|---|
| `suite_drums_kit_parity_tests` | **108 renders bit-identical to the DRM-00 reference on all 10 buses** (main, Send 1/2, Out 2..8), comparing the legacy kit (`legacyKit()`, 16 voices, one voice per pad) with the reference module given the same values. It covers 12 scenarios (listed below) × 44.1/48/96 kHz × 64/333/512 frames. |
| `suite_drums_kit_engine_tests` | All 8 models sound on all 16 pads. At tune ±12 st, fine ±100 ct and character 0/1, at 44.1 and 96 kHz, every model is finite and bounded. Perc's character changes its timbre. Notes can be remapped; a shared note goes to the lowest pad. Choke groups work on any pads and can be disabled. Polyphony 1/2/4/8 holds as many hits; 128 hits fill exactly 64 voices. Sample players: A only, velocity below and from the split, mix, and both players reported in use. Output is bit-identical with 64/512/20000/37000-frame blocks. The allocation probe counts 0 allocations in `process()` with poly 8, sequencer, MIDI and panic. |

The 12 parity scenarios:
- defaults, normal and dense MIDI;
- samples;
- the internal sequencer in Internal and Both mode, with a loop back;
- 6 random kits, with every legacy parameter random within its range:
  - filter types, transient, output bus, sends, kick body, choke, velocity;
  - automation at block starts;
  - CC120/CC123, panic, release stop/fade.

**Mutation checks:** a factor of 1.0000001 on the Send 1 path makes 54
renders fail; a change of the tom click constant makes 72 renders fail.

## Open decisions, for DRM-03b

- **Plugin parameter map for 16 pads.** The spec targets 160–240 host
  parameters (§6) and keeps structural data in versioned state. This needs a
  reviewed change of the DRM-01/02 map: 164 parameters, append-only IDs, not
  yet released. Proposal for DRM-03b:
  - host parameters per pad: level, tune, decay, pan, cutoff, send 1,
    send 2 and mute (8 × 16 = 128), plus kit and sequencer;
  - model, note, choke, polyphony, filter type, transient, body and
    sample-player settings move to the versioned `DPAD` state section.
- **Default kit for pads 9–16.** Currently Perc on notes 50–57, a
  placeholder. The final note map and labels are an open decision (03 §9).
