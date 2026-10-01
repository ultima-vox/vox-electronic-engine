# Accepted UI Reference Render Map

Status: Visual Gate A source of truth.

The accepted reference renders are the primary visual target for VOX Electronic Engine. The written design system and implementation standard define tokens, architecture, behavior, accessibility, host integration, and constraints; when a current JUCE layout visually conflicts with an accepted render, the implementation should be adjusted toward the render unless an explicit design revision is approved.

## Accepted instrument references

- Psy Bass — `image-gen-1(1).png`
- Acid — `image-gen-3(1).png`
- Lead — `image-gen-2(1).png`
- Atmos / Texture — `image-gen-5.png`
- FX Engine — `image-gen-4.png`
- Arp / Sequence — `image-gen-7.png`

Historical/reference-only:

- Drums — `image-gen-6.png` (drums are now a separate product/domain and are not part of the Electronic Engine rack implementation target).

## Cross-screen visual grammar

1. Persistent top header with VOX identity, preset/project controls, Seed, MIDI/CPU/output status, Panic, Settings.
2. Left Instrument Rack is a first-class visual structure, not a text list. Occupied slots use an identity thumbnail, slot number, instrument name, secondary descriptor/version, MIDI channel, and power/activity state. Empty slots are visually quieter.
3. Selected instrument header is large and clear, with instrument identity on the left, preset navigation in the centre/right, favorite/navigation affordances where applicable, and channel selection on the right.
4. Main navigation sits directly below the instrument header.
5. Each instrument has a wide identity/hero artwork band below navigation. This artwork is instrument-specific and participates in recognition; it is not a generic waveform strip.
6. Workspaces are dense and intentional. Large empty panels, oversized dead zones, floating controls, and generic equal-card grids are rejected.
7. Graphs are used where the domain benefits from them: oscillator waveform, filter response, envelope, modulation curve, spectrum/cloud, XY pad, modulation matrix, sequencer lanes.
8. Controls are grouped around functional sections. Knobs do not replace the visual hierarchy; they support it.
9. Bottom performance area keeps keyboard/chords/scale controls and the piano keyboard visually integrated with the instrument workspace.
10. Cyan remains the global interaction colour. Instrument/domain accents may differentiate identity while preserving VOX family coherence.

## Psy Bass target

Reference: `image-gen-1(1).png`.

Composition:

- Rack with image thumbnails and selected-slot identity.
- `PSY BASS` hero artwork: dark futuristic landscape / deep-frequency identity.
- Top row: Oscillator, Filter, Amp Envelope.
- Middle row: Drive / Character, Accent, Performance.
- Bottom row: Modulation + Matrix.
- Oscillator and modulation areas are graph-led; Filter and Envelope combine a graph with a compact control strip beneath it.
- Matrix is table-like and information-dense, not a decorative dot grid.
- The target is balanced density: nearly all visible space has a functional or identity role.

## Acid target

Reference: `image-gen-3(1).png`.

Composition:

- `ACID` hero artwork with 303 identity and green domain accent.
- Compact upper control band with non-uniform widths: Oscillator, Filter, Envelope, Distortion / Drive, Accent, Slide, Output.
- Large central Step Sequencer is the dominant workspace and includes Note / Accent / Slide / Gate / Octave lanes across 16 steps.
- Bottom row: Modulation, Performance, Play Mode.
- Acid must not look like Psy Bass with relabelled knobs. Its sequencer-centric structure is part of the instrument identity.

## Visual Gate rule

Gate A screenshots must be judged against these accepted renders, not merely against previous implementation screenshots. Passing CI proves build/test/render integrity only; it does not grant visual acceptance.

Required canonical captures remain:

- Psy Bass SOUND — 1180x760
- Psy Bass SOUND — 1500x920
- Acid SOUND — 1500x920
- Psy Bass MACROS — 1500x920

Explicit owner approval is still required before Functional Gate B.