# DRM-04a: VOX Drums sequencer model, scheduler and MIDI export

Status: on `drm/04-sequencer`, stacked on DRM-03b. DRM-04a is the engine,
state and export slice of DRM-04 (03 §55, §62, §104). DRM-04b, the next PR,
adds the step editor (grid, per-step values, bar selector, duplicate, A/B,
undo/redo) and the MIDI export button and drag in the editor.

## Pattern model

| | DRM-03 | DRM-04 |
|---|---|---|
| Slots | 5 fixed factory patterns | 5 **editable slots** (`drm.seq.pattern` selects one), filled with the factory patterns in a new instrument |
| Lanes | 8 | **16** (one per pad) |
| Grid | 16 steps × 4 bars | **16, 32 or 64 steps per bar** (1/16, 1/32, 1/64), **1–4 bars** |
| Step | on, accent, velocity | plus **probability** 1–100 %, **ratchet** 1–8, **microtiming** ±50 % of a step |
| Randomness | none | probability decided by the slot's **seed**, the step's absolute position and the lane: deterministic, the same on every play and in the export |

Saved as the versioned state section **`DSEQ` v1**:
- 5 slots, each with steps, bars, seed and every cell.
- Strict validation: a wrong version or slot count, a grid other than
  16/32/64 steps × 1–4 bars, or a value out of range rejects the document,
  and the live state is kept.
- A document without `DSEQ` (DRM-01..03) gets the factory patterns.

**Threads:** `PatternStore` is a SpinLock held for a copy. The audio thread
copies the selected slot with a try-lock, only when that slot changed or
another slot was selected. It never waits; if the lock is busy, it keeps
playing what it has.

## One rule for playback and export

`kit::stepHits (pattern, context, g)` gives the hits of grid step `g` (an
absolute index from song position 0). The rule:
- odd steps are late by swing;
- accent raises velocity towards 127 by the accent amount;
- micro offsets the step;
- a ratchet splits the step evenly;
- probability compares a hash of seed, step and lane with the percentage;
- disabled lanes play nothing.

With ratchet 1, micro 0 and probability 100, the time and velocity are
exactly those of the legacy module.

**Scheduler.** For each block, the engine evaluates the steps that can reach
it (3 steps back, for ratchets, micro and swing). It plays every hit whose
frame offset falls in the block (legacy rounding) and that is later than the
last hit played. A backwards jump (loop, seek) clears that horizon, so the
hits play again. The hits are ordered by offset, stably.

**MIDI export.** `exportMidi` writes an SMF format 0 file:
- 960 ticks per quarter, tempo, 4/4, channel 10.
- One Note On per hit at `round(ppq × 960)`, on the pad's current note.
- Each Note Off comes half a ratchet step later, or earlier if the same note
  starts again; at one tick, Note Offs come before Note Ons.
- Two lanes on one note at one tick become one MIDI note, with the louder
  velocity.
- Hits before position 0 are not written, because they do not play from 0.
- `DrumsProcessor::exportPatternMidi (slot, passes, bpm)` uses the current
  swing, accent, lanes and pad notes.

## Evidence (local, Linux GCC 13): 26/26 suite tests pass

| Test | Proves |
|---|---|
| `suite_drums_parity_tests`, `suite_drums_kit_parity_tests` | **Unchanged and passing**: the new scheduler plays the factory patterns bit-identically to the DRM-00 reference (81 plugin renders, 108 engine renders). |
| `suite_drums_kit_engine_tests` | **Step rule:**<br>• ratchet spacing<br>• micro ±50 %<br>• swing with micro<br>• accent value<br>• disabled lane<br>• 1/64 grid<br>• probability is deterministic per seed and position, plays ~30 % at 30 % (4000 passes), and another seed decides differently<br>**Scheduler = canonical list:** a random 16-lane, 32-step, 2-bar pattern with ratchets, micro, probability, swing, accent and a disabled lane, at 44.1/48/96 kHz × 64/333/512/4096 frames, with a loop back. Every canonical hit plays once at its sample and nothing else plays.<br>**Mutations:** a narrower look-back window → 12 failures; no horizon reset on a loop → 12 failures; wrong ratchet spacing → caught by the step rule. |
| `suite_drums_product_tests` | **Patterns:**<br>• a new instrument holds the factory patterns<br>• `DSEQ` round trip (16/32/64 steps, 1–4 bars, every field, seed) saves back byte-identically<br>• a document without `DSEQ` gets the factory patterns<br>• 16 malformed variants are rejected with the live state kept<br>• an empty slot is silent<br>• an edit made while playing is heard on its pad<br>**Export:** a readable SMF with 960 ticks, the tempo and channel 10. Note Ons equal the canonical hits (tick, note, velocity) over 3 passes with swing, accent, a disabled lane and a reassigned note. No overlapping notes, and every note ends. `resize` keeps grid positions.<br>**Allocation probe:** 0 allocations, now with pattern edits and slot changes between blocks. |

## Notes and open points

- `drm.seq.pattern` keeps its ID, range and choice names (the factory
  names). The slots start with those patterns; after editing, the name
  describes where the slot started. Renaming the choices is an owner
  decision (a `parameters.csv` change).
- Probability is decided per absolute step, so every pass of a pattern can
  differ, and the export of N passes writes exactly those passes.

## Test robustness note

One local `ctest -j6` run had a single failure of `suite_drums_parity_tests`.
Its output was not captured, and the cause is **not known**.

Repeated runs all passed:
- 3 × full `ctest -j6` under extra CPU load;
- 8 concurrent instances;
- 6 serial runs during sanitizer builds;
- ASan and TSan builds, with 0 reports.

One fragility was found and fixed: the test wrote its sample to a fixed
temp file name, so concurrent runs could delete each other's file. With a
fixed name, the 8 concurrent instances failed 96 checks as `file not found`;
with a unique name they pass.

If the failure shows up again (locally or in CI), it is to be investigated
as a real failure.
