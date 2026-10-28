# Full-on Drums — independent native drum kit (#71)

Status: **D1 and the D2 sequencer core implemented (development package,
not DAW-accepted).** Module
`libs/drums/VoxFullOnDrums.c`, InstrumentId `com.ultimavox.fullon-drums`,
package `com.ultimavox.fullon-drums.voxinst/0.1.0`. Loaded by the Rack's
native V2 connector (#68); not linked into the Rack, no second VST3.

## Model

One instrument in one Rack slot on one MIDI channel. Inside the module a
bounded pool of 16 one-shot voices plays the pads simultaneously. Stereo
output only (the Rack's existing slot bus, FX, ducking and mixer apply); no
per-pad outputs until a reviewed routing extension exists.

## Pads (stable identities; 8..15 may be appended later)

| Pad | Sound | Default note | Synthesis |
| --- | --- | --- | --- |
| 0 | Kick | 36 | Port of the frozen Checkpoint 7A KickSynth algorithm: exponential pitch sweep with curve, body + half-rate sub with its own tail, filtered noise click (4 ms), normalised tanh drive, 2.5 ms attack |
| 1 | Snare | 38 | Two detuned sines with a 10 ms pitch drop + high-passed noise; Snappy blends noise vs tone |
| 2 | Clap | 39 | Band-passed noise (TPT SVF), three bursts 10 ms apart then a decaying tail |
| 3 | Closed hat | 42 | Six square oscillators at 808 ratios + noise, two high-pass stages; choke group 1 |
| 4 | Open hat | 46 | As closed hat with a longer decay; choke group 1 |
| 5 | Tom | 45 | Sine with a 40 ms pitch drop (Sweep) + 2 ms click |
| 6 | Rim / perc | 37 | Two sines (1 : 3.3) + high-passed click |
| 7 | Cymbal | 49 | Metallic oscillators at a higher ratio + noise, high-passed |

Retriggering a pad fades its previous hit in 3 ms; a choke-group hit fades
the group's other voices in 3 ms (closed hat cuts open hat, *Hat Choke* on).
Decays are T60; voices end below −80 dB. NoteOff and sustain do not affect
one-shots; CC120 / Rack Panic / reset stop at once; CC123 and
`VOX_RELEASE_ALL_NOTES_AT_END` fade within 50 ms;
`VOX_RELEASE_ALL_SOUND_AT_END` (e.g. MIDI over the module budget) stops at once.

## Parameters (48 of the 64 pool endpoints; descriptor index = pool index)

| Pool | Endpoint | Pool | Endpoint |
| --- | --- | --- | --- |
| 5·pad + 0 | `<pad>.level` 0..1 | 40 | `kit.volume` 0..1 |
| 5·pad + 1 | `<pad>.tune` ±12 st | 41 | `kick.pitch_amount` 0..48 st |
| 5·pad + 2 | `<pad>.decay` T60 s (per-pad range) | 42 | `kick.pitch_decay` 5..200 ms |
| 5·pad + 3 | `<pad>.pan` −1..1 (constant power) | 43 | `kick.drive` 1..8 |
| 5·pad + 4 | `<pad>.character` 0..1 | 44 | `kick.sub` 0..1 |
| | | 45 | `kick.tail` T60 s |
| | | 46 | `kit.hat_choke` boolean |
| | | 47 | `kit.velocity` sensitivity 0..1 |

`<pad>` = kick, snare, clap, chat, ohat, tom, rim, cymbal. Pool 48–62 are the
D2 sequencer (below); 63 is free. Pads 9–16 need the larger host layout (96/128
experiment) for their own controls; their identities and state slots are
appended without renumbering. Parameter values are latched per hit.

## D2 sequencer (pool 48–62)

| Pool | Endpoint | Values |
| --- | --- | --- |
| 48 | `seq.mode` | Host MIDI (default) / Internal / Both |
| 49 | `seq.pattern` | Full-on Basic, Full-on Driving, Rolling Offbeat, Breakdown, Kick & Offbeat |
| 50 | `seq.swing` | 0..0.5 of a 16th, applied to odd 16ths |
| 51 | `seq.accent` | 0..1 velocity boost on accented steps |
| 52 | `seq.fill` | every 4th bar plays the pattern's fill |
| 53 | `seq.bars` | evolution length 1 / 2 / 4 bars |
| 54–61 | `seq.lane.<pad>` | lane on/off |
| 62 | `seq.steps` | 1..16 steps per bar |

Timing comes only from `VoxTransportV2` (tempo, ppq at sample 0, playing,
discontinuity): 16ths on the host ppq grid (4/4), sample position
`ceil((step_ppq − ppq0) · 60·fs/bpm)`. No wall clock or sample counting, so
any block split gives the same hits. A step plays at most once; a backwards
jump (loop, seek back) re-arms it; transport stop resets the Rack slot.

**No double triggers.** *Host MIDI* plays only incoming notes (sequencer
off). *Internal* plays only the sequencer and ignores incoming notes, so an
imported clip on the same track cannot double it. *Both* is explicit. The
Rack's own pattern generator does not drive this slot (native descriptors do
not declare the sequence capability). A reopened project replays the same
pattern without duplicates (tested).

**Missing ABI hook (Architecture Guard).** Editing the step grid (per-step
velocity/accent, user patterns, MIDI export of the internal pattern) needs a
versioned content/editor table, e.g. `VoxContentV2 { getPattern(instance,
VoxPatternV2 *out); setPattern(instance, const VoxPatternV2 *in); }` called on
the controller thread, plus a pattern block in the module state. It is not in
`VoxModuleAbiV2.h`; no private cross-DLL calls are used instead. Until then
patterns are the five factory patterns selected by `seq.pattern`.

## State (module-owned, versioned, transactional)

v2, 280 bytes, little endian: `"VOFD"`, parameter count (63), pad count (8),
63 plain f32 values, 8 pad MIDI notes, 8 pad choke groups (0 = none, ≤ 4).
v1 (D1, 48 values) still loads; the sequencer values take their defaults.
`loadState` validates everything (finite in-range values, unique notes
< 128, choke ≤ 4) before applying anything. The Rack's named host values stay
authoritative on restore.

## Realtime

`process`/`reset`: no allocation, lock, logging, file I/O or DLL calls;
host records copied out with `memcpy` after stride/alignment checks;
per-sample event application, so the same input renders identically for any
block split. Deterministic noise (per-pad seed at trigger).

## UI plan (not implemented in D1)

D1 is played from the DAW piano roll and the Rack's generic controls (the
48 typed endpoints, grouped by pad). The drum UI, once a reviewed V2
editor/content hook exists:

1. 4×4 pad grid (pads 1–8 now, 9–16 later): click to audition, velocity by
   vertical click position, colour by choke group, live hit flashes.
2. Selected-pad editor: Level, Tune, Decay, Pan, Character (+ kick page for
   pitch envelope, drive, sub, tail); note assignment and choke group.
3. Kit strip: Kit Volume, Velocity Sens, Hat Choke, per-pad level meters.
4. D2 grid (needs the hook above): 8 (→16) lanes × 16 steps with
   velocity/accent per step, lane mute, swing, pattern slots and fills.

The note map and choke groups are already in the module state; editing them
needs the editor/content hook (no private cross-DLL C++ calls).
