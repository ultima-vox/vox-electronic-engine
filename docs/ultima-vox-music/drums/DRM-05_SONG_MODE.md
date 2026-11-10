# DRM-05 — Song Mode and full-composition MIDI export

**Status:** Owner-approved product requirement; design and implementation pending.  
**Tracking:** [#123](https://github.com/ultima-vox/vox-electronic-engine/issues/123)  
**Target:** `next/standalone-suite` (not `main`).  
**Milestone:** DRM-05 still includes the groove generator, presets and curated/licensed content defined in `../03_VOX_DRUMS_SPEC.md`; Song Mode is **additional**, not a replacement.

## Goal

A musician must be able to **write an entire 16-, 32-, or 64-bar drum composition** in VOX Drums and transfer the **whole composition as one Standard MIDI File**, either by:

1. **Clicking MIDI / Export** and saving a `.mid` file for import into a DAW; or
2. **Dragging MIDI** directly to a MIDI/instrument track in Cubase or REAPER.

The resulting clip contains the **full, editable song**, rather than only the current 1–4-bar pattern or a bounce to audio.

## Existing contract — keep working

- The current `PatternEditor` exposes five editable pattern slots, each with 16 pad lanes, 16/32/64 steps **per bar**, and 1–4 bars, plus A/B variations.
- `PatternEditor::writeMidi`, `exportForDrag`, and `DrumsProcessor::exportPatternMidi(slot, passes, bpm)` currently export one slot with one pass from the UI.
- Preserve Pattern mode, current per-pattern export behavior, all five existing slots, factory-content recall and `DSEQ` backward compatibility.
- This task is **not** the unrelated decision to rename the host choices of `drm.seq.pattern`; do not rename or remap host IDs as a side effect.

## Song mode — user experience

- Add a clearly identified **Pattern / Song** view and playback mode.
- Song length control: **16 / 32 / 64 bars** (initial 4/4 contract); timeline displays every bar, current position, clips and active loop region.
- Arrange pattern slots and A/B variations on the timeline; support insert/replace, duplicate, clear, copy/paste, bar navigation, undo and redo.
- **Every bar must be independently programmable.** Repeating five fixed source patterns with no way to edit an occurrence independently does not meet this requirement. Define an explicit unique-instance mechanism (e.g. `Make Unique` / copy-on-write clips or song-local bar steps) that works even if the user makes all 64 bars different.
- Pattern length and song length are separate concepts. Allow pattern clips to cross timeline bar boundaries only with specified behavior; handle last-bar truncation consistently in playback and MIDI export.
- Existing Pattern-mode MIDI export must remain available; Song mode must offer an unambiguous **Entire Song** export scope. Do not silently save only the selected pattern.
- Drag & Drop must continue to use a unique per-instance/per-drag temporary MIDI file (existing P2 fix); saving via dialog should not reuse temporary drag paths. Failure to export must be visible rather than producing an empty/partial song.

## Audio / event model

- The same **canonical scheduler** must drive live song playback and MIDI export; never reimplement probability, ratchets or microtiming separately for SMF.
- Define deterministic probability for repeated and independently edited placements. The same saved song/seed/position/tempo produces identical note events for playback and export; a deliberate seed change produces a documented new variation.
- Preserve each pad's MIDI note assignment, lane mute, velocity, accent, ratchet, microtiming, swing and pattern/clip variations. Define boundary behavior for ratchets, adjacent same-note events, Off-before-On ordering, stop/panic, tempo changes and transport seek/loop.
- Avoid playing the internal sequencer and re-imported MIDI simultaneously by accident: support and document Host MIDI vs Internal vs Both. The MIDI file is standard editable note data; no promise that JUCE auto-creates a DAW track.

## Single-file export contract

- **Both UX paths export the entire selected song** (16/32/64 bars) as exactly one `.mid` file, using the **same export function and frozen song snapshot**; bytes must match for identical snapshot and BPM.
- SMF format 0 / one track / MIDI channel 10 is acceptable for 1.0. Include tempo, 4/4, track name, all Note Ons and Note Offs, pad notes and velocity, proper event sorting, and End-of-Track at the full requested song duration.
- Retain 960 ticks/quarter as in DRM-04. Playback vs exported event timing tolerance <= 1 SMF tick; note and velocity exact. For repeated hits on one note, prevent overlapping/stuck notes.
- Export may allocate and perform IO **only outside the realtime audio callback**, using a consistent captured song state. There must be no hidden four-bar limit and no note truncation at bar 16, 32 or 64.

## Data model and realtime rules

- Add a **bounded, versioned song representation** and validated project-state section. Preserve existing `DSEQ` v1/v2 pattern data and stable automation IDs; legacy projects reopen as before and do not silently acquire a changed arrangement.
- Editing, copying clips, export snapshot creation, file IO and memory allocation are **non-audio-thread** work. Publish complete immutable/prebuilt song snapshots to the audio thread with a bounded ownership/lifetime strategy; no blocking mutex, allocation, IO or unbounded scan in `processBlock`.
- For a worst-case **64 bars × 16 lanes × 64 steps**, record explicit upper bounds for memory and scheduling work. No 64-bar stack aggregates in plugin constructors, `setStateInformation` or host callbacks (DRM-04b stack-overflow regression).
- Project restore of unknown/malformed song data must be atomic and fail safely without altering the live project; test backwards compatibility, edit while running, loop/seek and preset reopen.

## Acceptance criteria

1. A fresh instrument can create and edit 16/32/64 bars; every bar can be independently changed; A/B variations and unique occurrences work, including a unique note in the final bar of a 64-bar song.
2. Pattern mode remains correct: existing 1–4-bar export and old project content unaffected.
3. Save/restore preserves full song state, seeds, unique placement edits, clip order, automation and playhead/loop behavior; hostile/truncated state leaves existing state intact.
4. Clicking MIDI Export and dragging MIDI produce **one full-length, byte-identical SMF** for identical captured state and tempo; parse independently, verify End-of-Track at bar 16/32/64, final-bar notes present and no stuck Note Offs.
5. **End-to-end Internal playback ↔ independent SMF parse parity** on a 64-bar song with unique final-bar data, different clip lengths, A/B, swing, ratchets, probability, microtiming, lanes 9–16 and host-note remaps. Test 44.1/48/96 kHz × block 64/333/512, stop/seek/loop and tempo changes. Time error <= 1 tick.
6. Prove **0 allocations, blocking locks and IO** in realtime callback during long-song playback, transport jumps and concurrent non-RT edits. Stress and sanitizer suites remain clean.
7. Windows self-hosted exact-HEAD Suite and Build plus Steinberg validator succeed.
8. Manual **Cubase 12–15 and REAPER** checks: export via click and Drag & Drop into MIDI/instrument track, verify 64-bar duration, editable notes, tempo, loop, project reopen and no double triggering in Host MIDI mode. **DRM-08 release gate remains HOLD** until this manual acceptance and remaining release criteria are completed.

## Implementation plan (suggested split)

1. **DRM-05 Song contract:** document timeline/state/seed/clip ownership/RT publication and migration; update test plan.
2. **DRM-05 Song engine:** arrangement model, stable per-placement edits, canonical scheduling, state and non-RT capture; add parity/stress tests.
3. **DRM-05 Song UI + export:** 16/32/64-bar timeline, navigation/editing, **Entire Song** button export and file drag, Windows tests.
4. Integrate with the existing DRM-05 groove generator and curated content, then complete DRM-08 DAW acceptance.

**No code is implemented by this document; it is the owner's approved target and the acceptance contract for subsequent PRs.**
