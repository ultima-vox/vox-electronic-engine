# VOX ELECTRONIC ENGINE — Production UI Implementation Standard v1.2

**Status:** CANON / normative supplement  
**Scope:** production implementation of the accepted Vox Electronic Engine UI in JUCE  
**Depends on:** `UI_DESIGN_SYSTEM.md`, `UI_COMPONENT_CATALOG.md`, `UI_VISUAL_REFERENCE_SPEC.md`  

This document closes the gap between the existing design language and what is acceptable in code. It exists specifically to prevent a functionally correct but visually generic JUCE interface from being treated as finished.

---

# 1. Acceptance principle

A page is not accepted because it contains the requested controls.

A production page must satisfy all four contracts simultaneously:

1. **Visual contract** — hierarchy, proportions, density, typography, depth and component grammar match the accepted Vox reference.
2. **Functional contract** — visible production controls operate real state/DSP/automation.
3. **Architecture contract** — no violations of Rack/provider/state/realtime/automation boundaries.
4. **Host contract** — the result behaves correctly in Cubase and survives save/reopen/automation/routing workflows.

`compiles`, `tests pass`, `all controls exist` and `uses cyan` are not visual acceptance criteria.

---

# 2. Source-of-truth order

When implementation choices conflict, use this order:

1. `UI_DESIGN_SYSTEM.md`
2. `UI_COMPONENT_CATALOG.md`
3. `UI_VISUAL_REFERENCE_SPEC.md`
4. this implementation standard
5. page-specific implementation

The accepted visual reference is a **design specification**, not a mood board.

Do not replace its composition with a generic card grid merely because that is easier to implement with JUCE layout helpers.

---

# 3. Explicitly rejected visual patterns

The following are not production-quality Vox UI:

- stock JUCE `Slider`, `ComboBox`, `TextButton` appearance;
- `drawEllipse()` + cyan arc presented as a finished knob;
- large empty panel containing one centered knob;
- same generic panel template for every instrument;
- every label rendered at nearly the same visual weight;
- rack rows that resemble a spreadsheet or debug table;
- large stretches of unused space at wider resolutions;
- fake oscilloscope/filter/envelope curves unrelated to state;
- cyan borders/glow on nearly every object;
- raw SlotId/internal/debug identifiers in the primary workflow;
- giant permanent red Panic control;
- layout that simply scales coordinates proportionally with window size;
- UI shadow state that diverges from engine state;
- decorative controls that have no production binding after visual acceptance.

If the result still reads as a developer/debug UI, the visual gate is failed.

---

# 4. Visual hierarchy

Every synth-like page must read in this order:

```text
1. Product / project context
2. Selected rack instrument
3. Instrument identity / preset
4. Primary sound-shaping modules
5. Secondary sound/performance modules
6. Modulation / pattern / routing context
7. Performance keyboard
```

The eye must not encounter a uniform wall of equally important cards.

Use size, typography, visual weight, grouping and graph area to establish hierarchy.

---

# 5. Density targets

Vox is a dense workstation UI. Density must come from meaningful information, not tiny text.

## 5.1 Compact — 1040 × 680

- rack remains visible;
- hero may collapse or reduce substantially;
- secondary module details may collapse into tabs/disclosure;
- primary controls remain directly accessible;
- keyboard may reduce height;
- no horizontal overflow;
- no unreadable labels;
- no proportional shrinking below usable hit targets.

## 5.2 Standard — 1180 × 760

- complete primary workflow visible;
- hero remains shallow;
- primary graphs visible;
- secondary controls visible without wasting large areas;
- keyboard persistent.

## 5.3 Large — 1500 × 920

Larger size must reveal **more useful musical context**, not merely increase whitespace.

Possible additions:

- wider graphs;
- visible modulation section;
- more detailed matrix rows;
- expanded secondary controls;
- richer meter/context views.

Do not scale card emptiness.

---

# 6. Spacing and geometry tokens

Base unit: `4 px`.

```text
space.1  = 4
space.2  = 8
space.3  = 12
space.4  = 16
space.5  = 20
space.6  = 24
space.8  = 32
```

Production defaults:

```text
panel gap                8–12 px
panel internal padding  10–12 px
section header height   24–28 px
button height           28–32 px
selector height         28–32 px
main tab height          34–38 px
rack slot height         52–58 px
```

Minimum interactive hit target should normally remain around `28 × 28 px` even if visual art is smaller.

Avoid arbitrary per-page magic numbers. Page geometry may be instrument-specific, but spacing comes from shared tokens.

---

# 7. Elevation and surface model

The UI is dark and layered, not flat and not glassmorphic.

Canonical surface stack:

```text
Window background
  ↓
Workspace surface
  ↓
Panel surface
  ↓
Raised/active control surface
  ↓
Focused/selected state
```

Allowed techniques:

- low-contrast vertical gradients;
- 1 px inner/outer separators;
- subtle top-edge highlight;
- very restrained shadow under raised controls;
- active cyan edge treatment;
- soft glow only for current/active information.

Forbidden:

- strong drop shadows on every card;
- heavy blur;
- neon glow around all controls;
- glossy skeuomorphic plastic;
- photorealistic hardware textures.

---

# 8. Typography hierarchy

Use a deliberately limited hierarchy.

```text
Product / Brand        18–22 px, semibold/bold
Instrument Title       22–28 px, semibold
Instrument Subtitle    11–13 px, regular
Section Title          12–14 px, semibold
Parameter Label        10–12 px, medium
Parameter Value        10–12 px, regular/medium
Metadata / Status       9–11 px, regular
```

Rules:

- not everything is uppercase;
- section titles may use uppercase/small caps;
- instrument title must be visually stronger than section titles;
- parameter values must remain readable without hover;
- muted text must remain legible on calibrated dark displays;
- avoid truncating parameter units where layout can reasonably accommodate them.

---

# 9. Iconography

Use one icon family/grammar.

Icons must be:

- vector where practical;
- optically aligned;
- consistent stroke weight;
- understandable without decoration;
- paired with tooltip where meaning is not obvious.

Do not mix emoji, Unicode symbols, raster icons and unrelated SVG styles in the production UI.

Icon colour follows state tokens, not per-icon invention.

---

# 10. VoxKnob — production anatomy

`VoxKnob` must be a designed control, not a recoloured stock rotary.

## 10.1 Layer stack

Render in this order:

```text
1. optional modulation range/ring
2. inactive value track
3. active value track
4. outer housing ring
5. recessed knob body
6. subtle body highlight/shading
7. position indicator
8. focus/automation/learn state
9. label
10. formatted value
```

## 10.2 Sizes

```text
Small   34–38 px body
Medium  44–50 px body
Large   58–66 px body
```

Use Large only for primary parameters. A page made almost entirely of Large knobs is rejected.

## 10.3 Visual behaviour

Normal:
- dark body;
- low-contrast inactive arc;
- cyan active arc.

Hover:
- small contrast increase;
- no dramatic glow.

Dragging:
- stronger active arc/pointer;
- current value clearly visible.

Focused/automated:
- separate focus indication; do not overload base value colour.

Modulated:
- base value and modulation range must be distinguishable.

Disabled:
- reduced contrast while preserving labels.

## 10.4 Interaction behaviour

Required where compatible with engine semantics:

- drag;
- fine adjustment modifier;
- double-click/default reset;
- wheel only where intentional;
- begin/end host automation gesture;
- context/tooltip showing exact value/unit;
- keyboard accessibility where practical.

---

# 11. VoxPanel — composition rules

A panel exists because it owns a coherent musical function.

It must not be a generic rectangle used to fill grid cells.

Canonical anatomy:

```text
header: status/power | title | optional selector/action
body:   graph/primary controls
footer: secondary controls / values only when needed
```

A panel with one small control and >50% dead area should be redesigned or merged with a related panel.

Primary graph-driven modules should allocate meaningful space to the visualization.

---

# 12. Oscillator panel

Where supported, use a graph-led composition.

Recommended hierarchy:

```text
Waveform / wavetable display
Wave/type selector
Tune / octave / semitone / fine
Blend / phase / morph / related supported controls
```

The graph reflects actual oscillator state or a deterministic snapshot derived from it.

Do not render a generic sine wave when the selected oscillator is saw/square/wavetable/noise.

---

# 13. Filter panel

Recommended hierarchy:

```text
Filter type / slope
Response graph
Cutoff (primary)
Resonance
Envelope amount
Key tracking
Drive where filter-owned
```

The response graph must be computed from current filter semantics closely enough to communicate real behaviour. It must not be a decorative Bézier curve.

---

# 14. Envelope editor

`VoxEnvelopeEditor` is both visualization and editor when the underlying parameter contract permits direct editing.

Required:

- current A/D/S/R reflected immediately;
- draggable handles if direct manipulation is enabled;
- parameter gestures propagated correctly;
- stage/value labels readable;
- graph and knob/value controls remain synchronized;
- no second independent envelope state.

---

# 15. Modulation controls

## 15.1 LFO / modulation graph

Display must reflect:

- actual waveform/shape;
- bipolar/unipolar semantics;
- phase where relevant;
- sync/free state;
- current rate/division.

## 15.2 Modulation Matrix

Canonical row:

```text
Source | Transform/Polarity | Amount | Destination | Enable/Delete
```

Rules:

- source list from capabilities;
- destination list from modulatable descriptors;
- compact row geometry;
- amount control may be knob or bipolar field/slider depending density;
- active routes visually stronger than unused rows;
- matrix must not look like a generic settings table.

---

# 16. Instrument Rack

The Rack is an instrument browser/control surface, not a table.

Occupied slot anatomy:

```text
slot number
identity thumbnail/icon
instrument name
preset/subtitle
MIDI channel
activity
power/state
```

Selected slot:

- visible outline/edge treatment;
- brighter surface;
- state marker independent of colour alone.

Empty slot:

- strongly reduced visual weight;
- no fake thumbnail;
- `Empty` / `OFF` only where applicable.

Debug identifiers are not shown in primary slot presentation.

---

# 17. Instrument Hero

A shallow hero/identity banner is permitted and expected where it improves identity.

It contains:

```text
LEFT   instrument title + short descriptor
CENTER low-contrast identity artwork/visual
RIGHT  concise capability/character metadata
```

It is not a marketing billboard and must not consume workspace needed for musical controls.

At compact breakpoint it may collapse.

---

# 18. Header

Global header is one compact line.

Expected order:

```text
VOX brand
Prev / Next
Project/Preset selector
Save
Seed / Randomize where valid
MIDI
CPU
Output
Panic
Settings
```

Rules:

- status items visually quieter than actions;
- Panic compact and red only as a danger action;
- output is compact, not a large panel;
- branding must not compete with the selected instrument title.

---

# 19. Keyboard / performance footer

The footer remains stable across synth-like Parts.

Contains where supported:

- pitch bend;
- modulation wheel;
- piano keyboard;
- pressed-note visualization;
- octave labels;
- keyboard/MIDI-learn context.

Selected-slot audition semantics must remain intact.

Visual customization must not bypass existing note ownership, NoteOff or Panic behaviour.

---

# 20. Instrument-specific composition

The shell is shared. The center workspace is not forced into one universal arrangement.

Allowed pattern:

```text
Shared shell
  ├── Rack
  ├── Header
  ├── Tabs
  ├── Footer
  └── descriptor/capability-driven workspace
```

Psy Bass, Acid, Lead, Atmos and FX may use different panel compositions while sharing primitives.

Do not spread concrete instrument checks through `PluginEditor`.

Instrument-specific page composition belongs behind the provider/descriptor/UI-factory boundary or another architecture-consistent equivalent.

---

# 21. JUCE implementation standard

JUCE is the framework, not the visual language.

It is acceptable to reuse JUCE controls for behaviour, focus, accessibility and parameter attachment, while completely replacing their painting.

Production implementation should favour:

- custom `LookAndFeel`;
- dedicated Vox components;
- vector `Path` drawing;
- reusable SVG assets;
- cached static artwork;
- explicit repaint regions;
- timer rates appropriate to the displayed data;
- immutable/snapshot data crossing into visualization code.

Do not use OpenGL simply to make the UI look modern. Use it only if profiling demonstrates a real need for a high-rate visualization.

---

# 22. Rendering/performance budget

UI must never compromise audio realtime behaviour.

Rules:

- no UI rendering on audio thread;
- no locks introduced into `processBlock` for visualization;
- no allocation in warmed audio path;
- meters/graphs consume bounded snapshots/atomics/ring buffers;
- expensive static art is cached;
- repaint only dirty regions where practical;
- do not refresh all graphs at display refresh rate if their data does not change that quickly;
- hidden/collapsed pages should stop unnecessary timers/repaints.

Suggested starting update rates:

```text
Meters             30–60 Hz where necessary
Waveform state     on parameter/state change
Filter response    on relevant parameter change
Envelope graph     on relevant parameter change
CPU/status         5–10 Hz
MIDI activity      event-driven + short decay timer
```

These are implementation defaults, not realtime-thread requirements.

---

# 23. Parameter binding rules

After visual approval, every production control must bind to real state.

No final shipping control may be:

- decorative;
- disconnected;
- backed only by local UI state;
- mapped to a different parameter merely because its label looks similar.

Bindings must preserve:

- stable host parameter IDs;
- begin/end gestures;
- automation playback;
- preset restore;
- project save/reopen;
- selected-slot context changes;
- descriptor capability filtering.

---

# 24. Visual-prototype exception

During **Visual Gate A only**, local design data may be used to render realistic examples of components not yet wired.

Conditions:

- mock state is clearly identified as visual-gate-only;
- it never enters audio/state serialization;
- it is removed or replaced before functional acceptance;
- screenshots state which components are still mock-driven.

This exception exists so visual composition is not constrained by incomplete DSP during design review.

It does not permit fake production functionality.

---

# 25. Visual Gate A

Before final DSP integration of a redesigned page, provide screenshots at minimum:

```text
Psy Bass / SOUND / 1180×760
Psy Bass / SOUND / 1500×920
Acid     / SOUND / 1500×920
Psy Bass / MODULATION or MACROS / 1500×920
```

For each relevant screen, compare:

```text
APPROVED REFERENCE | IMPLEMENTATION
```

Visual review checks:

- composition;
- density;
- whitespace;
- hierarchy;
- typography;
- knobs;
- graphs;
- rack;
- header;
- instrument identity;
- responsive behaviour;
- absence of generic JUCE appearance.

Gate A requires explicit owner visual approval.

Do not declare UI complete before that approval.

---

# 26. Functional Gate B

After Gate A approval:

- connect visible controls to real descriptors/state/DSP;
- remove temporary visual-only data;
- verify automation;
- verify graph/state synchronization;
- verify presets;
- verify save/reopen;
- verify selected-slot refresh;
- verify routing isolation;
- verify Panic;
- run full automated suite;
- run manual Cubase acceptance.

Any visible control that remains fake is a blocker.

---

# 27. Screenshot review checklist

A reviewer should answer these questions before accepting a screen:

```text
[ ] Does the page have an obvious visual focal point?
[ ] Can the selected instrument be identified instantly?
[ ] Are primary controls visually stronger than secondary controls?
[ ] Are graphs meaningful and domain-specific?
[ ] Is there excessive dead space?
[ ] Do large resolutions reveal more useful information?
[ ] Do knobs look like a Vox component rather than stock JUCE?
[ ] Is typography hierarchical rather than uniform?
[ ] Does the rack read as loaded instruments, not table rows?
[ ] Are active/selected/focused states obvious without excessive glow?
[ ] Are all normal labels/values readable without hover?
[ ] Is any debug/internal information leaking into the normal workflow?
[ ] Does the screen remain recognizably Vox when instrument identity accent changes?
```

Any significant `no` requires another visual iteration.

---

# 28. Code review checklist

```text
[ ] Uses shared Vox components/tokens
[ ] No stock JUCE production appearance
[ ] No page-local colour inventions
[ ] No duplicated parameter state
[ ] Stable automation IDs unchanged
[ ] Descriptor/capability driven where required
[ ] No audio-thread lock/allocation/I/O regression
[ ] Graphs consume safe state snapshots
[ ] Hidden UI does not waste repaint/timer budget
[ ] Responsive breakpoints deliberately implemented
[ ] Project/preset restore verified
[ ] Cubase manual gate identified where required
```

---

# 29. Definition of done

The UI implementation is done only when all of the following are true:

- approved reference and implementation are in the same visual class;
- owner passes Visual Gate A;
- every production control is functional;
- graphs represent real state;
- responsive layouts are deliberate at compact/standard/large sizes;
- state/automation/migration contracts remain intact;
- realtime guarantees remain intact;
- automated tests are green;
- Cubase manual acceptance passes.

A merely functional interface is not production UI.
