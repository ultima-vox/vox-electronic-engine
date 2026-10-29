# Pattern and content ABI (V2.1)

Status: owner-approved architecture (#78, 29 Oct 2026). The structs are in
`libs/instrument/abi/VoxModuleAbiV2.h`; the host model is
`libs/pattern/Pattern.h`. This slice adds the contract, its validation and
the snapshot hand-off. Rack persistence, Drums playback, MIDI export and the
grid UI land in the next slices (see Delivery).

## One model for every instrument

A pattern is `laneCount × stepsPerBar × barCount` cells, stored
`[lane][bar][step]`. The ceiling for 1.0 is 16 lanes × 64 steps × 4 bars
(4096 cells). Each instrument declares what it really supports in
`VoxPatternCapsV2`:

| Field | Meaning |
| --- | --- |
| `playback` | `HOST`: the Rack's scheduler plays lane 0 as notes. `INTERNAL`: the module sequences the snapshot itself. Exactly one. |
| `laneCount`, `maxStepsPerBar`, `maxBars` | Real dimensions. Drums: 8 × 16 × 4. Acid note lane: 1 × 16 × 4. No fake extra lanes. |
| `stepFields` | Which step fields the instrument reads. A field it does not read must hold its canonical default, so no dead data is stored or shown. |
| `laneNames`, `laneNotes` | One name per lane. `INTERNAL` lanes also declare distinct MIDI notes (export and host MIDI map). |

A cell (`VoxPatternStepV2`, 16 bytes) has an ON flag (otherwise a rest), plus:
- accent, slide and tie flags;
- velocity 1–127;
- note −48..48 and octave −4..4, stored separately;
- gate length and slide time, each 1/256 step;
- probability 0–100 % and ratchet 1–8.

The Acid five-lane NOTE / ACCENT / SLIDE / GATE / OCTAVE workflow is one
HOST note lane with `NOTE | OCTAVE | ACCENT | SLIDE | GATE` (plus `VELOCITY`,
`TIE`). The Drums grid is eight INTERNAL lanes with `VELOCITY | ACCENT`. A
rest cannot carry accent, slide or tie.

## Authority

The **Rack is the only durable owner** of a slot's pattern. It holds the
edits, persistence, MIDI export and host sequencing.
- **Module side.** A module never saves its own competing copy. An INTERNAL module keeps only a runtime copy of the latest snapshot, refreshed when `patternGeneration` changes.
- **HOST instruments** (Acid, Bass) never sequence notes themselves; the Rack's single scheduler does.
- **Serialized field.** The current pattern is stored in the slot's `patternPayload` as `VPT2` encoding version 1, with only the used cells (10 bytes each). That encoding is the stored pattern; the next slice wires it into the slot state.
- **Load, undo, redo and reinstall** replace the Rack copy transactionally and publish a new snapshot. The module's runtime copy is rebuilt from that snapshot, never the other way round.

## Realtime hand-off

`pattern::SnapshotBuffer` is a triple buffer:
- **Message thread.** Validates an edit, copies it into the buffer that neither the audio thread nor the pending slot holds, and publishes it with a new, strictly increasing generation.
- **Audio thread.** Picks up the newest snapshot once per block and keeps it until the next block.
- **Guarantees.** No locks, no heap, no file access, no silence on contention. Rapid edits never overwrite the snapshot being read; a stress test with 20,000 publishes checks for torn reads.
- **To the module.** The snapshot is handed over in the appended `VoxProcessContextV2.pattern` / `patternGeneration` fields.

An INTERNAL module derives its step position from the transport PPQ, so a
new pattern applies at the next block without restarting from step 0. BPM,
loop, seek and stop/start keep their musical position.

## Note output and MIDI export

An INTERNAL module reports the notes it generates through the appended
`VoxProcessContextV2.noteOutput` (`VoxNoteOutputV2`):
- **Buffer.** The host owns the buffer and sets its capacity and stride.
- **Record.** The module writes `VoxNoteEventV2` records (sample offset, note, velocity as played, lane, accent) and the count.
- **What is reported.** Only notes the module generates itself; host MIDI is never echoed.
- **Host checks.** The host resets the count before every call. A count above the capacity voids that block's events, so stale buffer contents are never used. A failed or silenced block reports nothing. Out-of-order or out-of-block events are ignored by the consumers.

The same event path serves two consumers: same-block kick ducking (next PR) and MIDI export.

**Export plays the pattern instead of re-deriving it.**
- **Rendering.** `Rack::renderPatternHits` creates a fresh instance of the slot's own module with the slot's saved values and the Rack pattern. It plays one pattern pass offline (120 BPM, 48 kHz, so every 1/960-quarter tick is a whole sample) and collects the reported notes.
- **The file.** The processor writes a type-1 SMF with one named track per lane, all on the slot channel.
- **Fidelity.** Swing, accent, lane switches and edited velocities are exactly what the kit plays. Positions round to 1/960 quarter.
- **Test.** The exported file, replayed through Host MIDI at 145 BPM, renders bit for bit like the internal sequencer.
- **Not included.** Host automation and modulation are not part of the exported pattern.

## ABI compatibility

`VoxProcessContextV2` grows only at its end (80 → 104 bytes: `pattern`, `patternGeneration`, `noteOutput`):
- **Readers accept the original size.** Every reader accepts `VOX_PROCESS_CONTEXT_V2_BASE_SIZE` (80) and reads `pattern` only when `structSize` covers it. Existing V2.0 modules keep working, and a V2.1 module still accepts a V2.0 host.
- **`VoxModuleV2.content`** is now a real table (`VoxContentApiV2`); `editor` stays reserved and must be NULL.

The loader rejects the instrument when anything in the content table is malformed:
- caps with unknown bits, out-of-range dimensions, missing names or duplicate notes;
- a factory pattern that fails validation;
- a module that tampers with the host buffer bounds;
- empty or duplicate factory names.

## Delivery

1. This slice:
   - the ABI structs;
   - the host model (validation, VPT2 codec, fingerprint, triple buffer);
   - the native loader import of caps and factory patterns;
   - snapshot delivery to `process`;
   - tests for layout, malformed input, hostile tables and concurrency.
2. Rack persistence and playback:
   - the slot pattern is held as VPT2;
   - the editing API;
   - Full-on Drums plays the snapshot through its internal sequencer and exports its factory patterns through `VoxContentApiV2`.
3. MIDI export from the same decisions (done, see above).
4. Editable 8×16 Drums grid UI, factory and user patterns, save/reopen and Cubase acceptance.
