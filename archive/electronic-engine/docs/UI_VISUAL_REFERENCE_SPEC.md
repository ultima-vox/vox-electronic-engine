# VOX ELECTRONIC ENGINE — Visual Reference Specification v1.1

**Status:** CANON / normative extension of `UI_DESIGN_SYSTEM.md`  
**Purpose:** make the accepted reference screens implementation-ready instead of leaving visual interpretation to an agent.  
**Applies to:** Vox Electronic Engine. Shared family rules may be reused by other VOX products, but product-specific workflows remain separate.

---

## 1. Source of truth

The accepted Electronic Engine reference set defines the intended production visual language for these workflows:

- Psy Bass
- Lead
- Acid
- Atmos / Texture
- FX Engine

The Arp / Sequence reference is retained only as a **workflow/editor reference for synth Pattern/Arp capabilities**. It is **not** a standalone rack instrument or engine identity.

The Drums reference belongs to **Vox Drums Engine**, a separate product. It is not part of the Electronic Engine product canon.

The references are not pixel-perfect implementation blueprints. They establish **hierarchy, density, component grammar, proportions, visual states, and interaction patterns**. The implementation must preserve those properties while remaining responsive and scale-safe.

Priority of authority:

1. `UI_DESIGN_SYSTEM.md` — global tokens, interaction rules, accessibility, realtime safety.
2. `UI_COMPONENT_CATALOG.md` — semantic component inventory and ownership.
3. `UI_VISUAL_REFERENCE_SPEC.md` — exact visual/layout grammar derived from accepted screens.
4. Product page implementations.

If a page implementation conflicts with this document, the page is wrong unless the design canon is intentionally revised.

---

# 2. Product-boundary canon

## 2.1 Electronic Engine rack identities

Electronic Engine rack slots represent actual Parts/instruments/effect modules such as:

- Psy Bass;
- Acid;
- Lead;
- Atmos / Texture;
- FX Engine or another explicitly supported electronic module.

A sequencing tool does not become a rack identity merely because it has a large editor.

## 2.2 Arp / Sequence ownership

Arpeggiator, phrase and sequence tools belong to the **currently selected synth/Part**.

They may be presented as:

- an `ARPEGGIATOR / PHRASE` panel inside `Sound`;
- the selected instrument's `PATTERN` page;
- an expanded Pattern editor;
- a piano-roll / lane editor associated with the selected Part.

They must not create:

- a separate `Arp / Sequence` rack slot;
- a separate hero identity;
- a separate MIDI Part unless the engine model explicitly defines one in the future.

Sequence state, preset state and generated material remain owned by the selected instrument/Part.

## 2.3 Drums ownership

Drums is a separate product: **Vox Drums Engine**.

The Electronic Engine must not implement:

- a Drums rack slot;
- drum-pad bank;
- drum kit editor;
- multi-row drum sequencer;
- drum-specific sample workflow

as Electronic Engine instruments merely because those mock-ups share the VOX visual system.

Drums may consume the same `vox-ui` primitives in its own repository/product.

---

# 3. Global shell canon

Every Electronic Engine screen uses the same shell.

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ Global Header                                                           │
├───────────────┬──────────────────────────────────────────────────────────┤
│ Instrument    │ Instrument Header                                       │
│ Rack          ├──────────────────────────────────────────────────────────┤
│               │ Main Navigation                                         │
│               ├──────────────────────────────────────────────────────────┤
│               │ Hero / Identity Banner                                  │
│               ├──────────────────────────────────────────────────────────┤
│               │ Active Workspace                                        │
│               ├──────────────────────────────────────────────────────────┤
│               │ Performance Footer / Keyboard                           │
└───────────────┴──────────────────────────────────────────────────────────┘
```

The shell is persistent. Switching instruments must not cause the entire plugin layout to jump.

## 3.1 Global Header

Canonical contents:

```text
Brand | Prev | Next | Project/Preset | Save | Seed | Randomize | MIDI | CPU | Output | Panic | Settings
```

Rules:

- one horizontal row;
- compact, dense, no oversized controls;
- brand occupies the left anchor;
- status indicators are visually quieter than actionable controls;
- `Panic` is the only persistent danger-red action;
- Output uses a compact knob + value rather than a full panel;
- MIDI activity uses a small cyan indicator;
- CPU is a compact status visualization, not a large meter.

## 3.2 Instrument Header

Canonical contents:

```text
Instrument Name + edit icon
Slot | MIDI channel | engine/preset subtitle
Instrument selector | previous | next | favourite | Channel selector
```

The title is the strongest typographic element below the brand.

## 3.3 Main Navigation

Canonical visual grammar:

- full-width horizontal segmented tabs;
- active tab uses cyan fill/edge treatment;
- inactive tabs remain dark;
- tabs are workflow-level, not individual parameter sections;
- geometry remains stable between instruments.

Primary synth grammar:

```text
SOUND | PATTERN | ROUTING | ZONES | MACROS | ADVANCED
```

FX-oriented modules may use:

```text
FX | ROUTING | MODULATION | MACROS | SEQUENCER | ADVANCED
```

`PATTERN` is the preferred home for expanded arp/sequence editing for synth Parts.

---

# 4. Instrument Rack canon

The left rail is a persistent 16-slot rack in Electronic Engine.

Each slot contains:

```text
slot number | identity thumbnail/icon | name      | MIDI channel | power
                                      | subtitle
```

## 4.1 Selected state

Selected slot:

- cyan outline;
- slightly brighter panel fill;
- thumbnail remains visible;
- active power icon cyan;
- name remains primary text;
- selected state must not depend on colour alone.

## 4.2 Empty state

Empty slots:

- no decorative thumbnail;
- `Empty` + `OFF` only;
- power indicator reduced/muted;
- visual weight significantly lower than occupied slots.

## 4.3 Identity thumbnails

Instrument thumbnails are allowed as small identity assets in the rack and hero banner. They are not control surfaces and must not carry essential state.

Suggested product accents:

- Psy Bass: cyan/blue;
- Acid: green;
- Lead: violet;
- Atmos: ice-blue;
- FX: cyan/blue.

There is no `identity.arp` or `identity.drums` token in Electronic Engine.

---

# 5. Hero / Identity Banner

Each major Electronic Engine instrument may use a shallow banner directly below the workflow tabs.

Purpose:

- rapid instrument identity;
- visual separation between shell and editor;
- short descriptive copy;
- product personality without changing the control language.

Canonical composition:

```text
LEFT: title + 2–3 line tagline
CENTER: low-contrast artwork
RIGHT: 3–4 keyword identity + short description
```

Rules:

- banner is decorative/identifying, never interactive;
- artwork must not reduce readability;
- artwork uses a dark overlay/low luminance behind text;
- banner height remains shallow relative to active workspace;
- banner must be removable/collapsible in compact layouts without losing functionality.

Pattern/Arp editors use the selected synth's identity. They do not get a separate Arp hero banner.

---

# 6. Panel grammar

Accepted screens use dense modular panels rather than one large undifferentiated surface.

Every functional panel follows:

```text
[power/status icon] SECTION TITLE                 [optional selector/action]
───────────────────────────────────────────────────────────────────────────
visualization / controls
labels
values
```

Rules:

- uppercase or small-caps semantic title;
- optional small icon;
- power/bypass at the left edge where relevant;
- optional selector/actions on the right;
- 1 px subtle blue border;
- radius approximately 4–6 px;
- narrow consistent gutters;
- no card shadows;
- multiple logical panels remain visible without scrolling at the 1440×1080 reference size.

---

# 7. Knob canon

The accepted references refine `VoxKnob`.

Rules:

- dark recessed centre;
- thin metallic/dark outer ring;
- cyan value arc;
- bright short position marker;
- no photorealistic knob texture;
- no giant glow;
- label and numeric value always readable without hover;
- small knobs are acceptable in dense workflow panels;
- modulation range is visually distinct from the base value.

Ordinary parameter knobs remain VOX cyan. Instrument colour is primarily for identity artwork and specialised graph content.

---

# 8. Graph canon

Shared rules:

- dark near-black graph well;
- subtle grid;
- cyan or instrument-accent curve;
- restrained low-opacity fill;
- data is primary;
- axes/labels only where useful;
- no fake animation.

## 8.1 Waveform / oscillator graph

- line waveform centred vertically;
- optional restrained curve glow;
- selector/navigation adjacent to the graph;
- waveform corresponds to selected/generated content.

## 8.2 Filter response

- logarithmic frequency horizontal scale;
- optional dB vertical scale;
- response curve with subtle area fill;
- filter selector in panel header.

## 8.3 Envelope

- editable node points where supported;
- selected node cyan/bright accent;
- ADSR stage letters aligned with numeric controls;
- visual curve and parameter state remain synchronized.

## 8.4 Modulation graph

Reusable graph for:

- ENV;
- LFO;
- stepped sequence;
- sample & hold/random.

Do not create a unique graph renderer per modulation source.

---

# 9. Footer / Performance keyboard

The accepted synth screens use a persistent bottom performance area.

```text
Keyboard | Chords | Scale tabs
Pitch wheel | Mod wheel | piano keyboard
right-side context: velocity curve / MIDI Learn / keyboard options
```

Rules:

- keyboard spans nearly the full content width;
- octave `C` labels are visible;
- pressed notes use cyan;
- Pitch and Mod remain narrow vertical controls;
- footer geometry remains stable between synth Parts.

---

# 10. Psy Bass page canon

```text
Row 1: Oscillator | Filter | Amp Envelope
Row 2: Drive/Character | Accent | Performance
Row 3: Modulation | Matrix
Footer: Keyboard
```

Required semantic groups:

- Oscillator;
- Filter;
- Amp Envelope;
- Pitch-related controls where supported;
- Drive / Character;
- Accent;
- Performance / Glide;
- Modulation;
- Mod Matrix;
- Keyboard.

Pattern generation and sequence editing for Psy Bass belong to its `PATTERN` workflow and state; they do not create another Part.

---

# 11. Lead page canon

Lead uses higher visual complexity and a violet identity accent in graphs/artwork.

```text
Row 1: Oscillator | Filter | Amp Envelope
Row 2: Unison/Voices | Glide/Portamento | Mod Envelope
Row 3: Macros | Arpeggiator/Phrase | Performance
Row 4: Modulation | Matrix
Footer: Keyboard
```

Semantic emphasis:

- supersaw/wavetable oscillator identity;
- morph / sync / FM / ring / noise where implemented;
- unison and stereo spread;
- glide / legato;
- modulation envelope;
- macro performance;
- arp/phrase integration;
- modulation matrix.

The compact `Arpeggiator / Phrase` panel is an embedded synth module. Opening its detailed editor switches to the Lead Part's `PATTERN` view rather than selecting another rack instrument.

---

# 12. Acid page canon

Acid is more sequencer-centric than the general synth layout.

```text
Top: Oscillator | Filter | Envelope | Drive | Accent | Slide | Output
Middle: Step Sequencer (wide)
Bottom: Modulation | Performance | Play Mode
Footer: Keyboard
```

Canonical Acid lanes:

- Note;
- Accent;
- Slide;
- Gate;
- Octave.

The sequencer is part of the Acid instrument. Pattern preset, sequence state and playback belong to Acid's selected Part.

Acid identity may use green in artwork/waveform cues while active controls remain VOX cyan.

---

# 13. Atmos / Texture page canon

Atmos is layered and visualization-heavy.

```text
Row 1: Layer A Sample/Granular | Layer B Texture | Layer Mix
Row 2: Filter | Amp Envelope | Motion Envelope | Spectral Cloud
Row 3: Shimmer/Reverb | Modulation | XY Texture Morph | Mod Matrix
Footer: Keyboard
```

Visual semantics:

- long waveforms are acceptable for layers;
- spectral graphics may be denser than synth waveforms;
- XY morph is first-class;
- long time values need sufficient label width;
- motion/evolution uses real state or safe visualization snapshots.

If Atmos gains sequencing, it is exposed as the selected Atmos Part's Pattern workflow.

---

# 14. FX Engine page canon

FX differs from synth pages because signal flow is primary.

## 14.1 Effect Chain

```text
IN → FILTER → DISTORTION → CHORUS → DELAY → REVERB → + → OUT
```

Rules:

- modules are cards;
- arrows show signal direction;
- selected module has stronger border/identity;
- bypass is obvious;
- serial/parallel mode where implemented;
- add/remove/reorder controls stay near the chain.

## 14.2 Module editor panels

Examples:

- Reverb with response/decay visualization;
- Delay with timing/feedback controls;
- Distortion with transfer curve.

## 14.3 FX performance

Valid workflow widgets:

- XY Performance;
- Modulation;
- Macros;
- Preset snapshots / A-B.

A sequencer/modulation timeline may exist as an FX workflow, but it belongs to the selected FX Part/module rather than a generic standalone Arp instrument.

---

# 15. Synth Pattern / Arp editor canon

The former `Arp / Sequence` mock-up is reclassified as the **expanded Pattern editor for the selected synth Part**.

It may be used by Lead, Bass, Acid, Atmos or future synth engines according to their capabilities.

Top transport/options row may contain:

```text
Play | Stop | Sync | Rate | Swing | Length | Octave | Scale Lock | Chord Mode | Randomize | Mutate
```

Main editor:

- left lane selector;
- central step/piano-roll grid;
- right pattern settings panel.

Supported lane families may include:

- Note;
- Gate;
- Velocity;
- Octave;
- Ratchet;
- Tie;
- Probability;
- Accent;
- Slide;
- Mod 1;
- Mod 2;
- Mod 3.

Rules:

- only lanes supported by the selected instrument are shown;
- active lane uses cyan selection treatment;
- note lanes use blocks positioned by pitch;
- velocity/probability use vertical bars;
- tie/ratchet use distinct symbolic grammar;
- modulation lanes use curves or stepped lines;
- playhead is synchronized to actual transport;
- editor header always shows the owning Part/instrument;
- switching rack Parts switches the Pattern data context;
- no independent `Arp / Sequence` preset/state namespace unless explicitly required by the instrument contract.

Compact Sound-page `Arpeggiator / Phrase` controls and the full Pattern editor operate on the same underlying synth sequencing state.

---

# 16. Accent-colour policy refined

Canonical rule:

- **VOX cyan** = global interaction, selection, focus, active controls;
- **instrument accent** = identity artwork, waveform/graph content, selected specialised module;
- **danger red** = Panic/destructive/error only;
- product accents never reduce state readability.

Electronic Engine identity accents:

```text
identity.psyBass     blue/cyan
identity.acid        green
identity.lead        violet
identity.atmos       ice-blue
identity.fx          cyan/blue
```

Arp/Pattern uses the owning instrument's identity accent. It has no independent identity colour.

---

# 17. Layout proportions

At the 1440×1080 reference canvas, use approximate proportions rather than literal fixed pixels:

```text
Global Header      ~6–7% of height
Instrument Header  ~7–8%
Main Tabs           ~3–4%
Hero Banner         ~12–13%
Footer Keyboard     ~11–13%
Instrument Rack     ~21–22% of width
Main Workspace      remaining width
```

Rules:

- rack width remains stable;
- footer height remains stable between synth-like instruments;
- panel geometry may vary by workflow;
- no element depends on the exact 1440×1080 size.

---

# 18. Responsive behaviour

When space decreases:

1. reduce decorative banner height;
2. reduce panel gutters/padding within token limits;
3. switch suitable controls from large to normal/small variants;
4. collapse secondary copy/metadata;
5. allow page-local scrolling only as a last resort;
6. never hide critical parameter values, bypass states, clip indicators, or selected Part identity.

When space increases:

- do not simply enlarge every knob;
- use extra area for graphs, spacing, labels and analyzer detail;
- preserve professional density.

---

# 19. Artwork asset rules

Artwork may appear in:

- hero banners;
- rack thumbnails;
- empty rack footer/brand area.

Artwork must never be baked into controls.

Requirements:

- separate asset from UI geometry;
- maintain dark overlay for text readability;
- scale/crop with aspect-fill semantics;
- no text embedded in artwork;
- no essential state encoded in imagery;
- fallback UI remains usable if artwork is missing.

---

# 20. Implementation mapping

Shared components required by Electronic Engine:

```text
VoxGlobalHeader
VoxInstrumentRack
VoxInstrumentSlot
VoxInstrumentHeader
VoxMainNavigation
VoxHeroBanner
VoxPanel
VoxSectionHeader
VoxKnob
VoxButton
VoxIconButton
VoxToggle
VoxSegmentedControl
VoxComboBox
VoxValueField
VoxGraph
VoxWaveform
VoxFilterResponse
VoxEnvelopeEditor
VoxTransferCurve
VoxMeter
VoxMacroPanel
VoxModulationTabs
VoxModulationMatrix
VoxXYPad
VoxKeyboard
VoxPitchBend
VoxModWheel
VoxEffectChain
VoxEffectSlot
VoxStepSequencer
VoxPianoRoll
VoxArpeggiator
VoxPatternEditor
```

Drum-specific components such as `VoxDrumPadBank`, `VoxDrumSequencer` and drum sample workflow components belong to Vox Drums Engine and are deliberately excluded from this Electronic Engine mapping.

---

# 21. Screenshot acceptance rules

Every significant UI PR must include screenshots at minimum:

```text
1440×1080 @ 100%
125% scale or equivalent high-DPI validation
one non-default selected instrument/workflow
```

For sequencing work, screenshot review must demonstrate the Pattern editor while a real synth Part is selected, proving ownership is visually unambiguous.

Review checks:

- shell alignment;
- consistent rack width;
- tab height/alignment;
- hero readability;
- panel gutters;
- knob size consistency;
- graph grammar;
- value legibility;
- no clipped labels;
- no default JUCE appearance leaking through;
- no standalone Arp/Sequence rack identity;
- no Drums workflow inside Electronic Engine.

---

# 22. Anti-patterns

Do not implement:

- giant knobs simply because space is available;
- one unique knob design per instrument;
- freeform panel placement without grid alignment;
- giant hero images that push functional controls below the fold;
- neon glow around every cyan element;
- graph animations unrelated to engine state;
- button-like styling for static labels;
- hidden numeric values that appear only on hover;
- inconsistent footer heights between instruments;
- arbitrary colour replacement of cyan for each product;
- `Arp / Sequence` as a standalone Electronic Engine Part;
- Drums as an Electronic Engine rack instrument.

---

# 23. Canonical rule

The Electronic Engine is **one host shell with multiple electronic Parts and their own workflows**.

A user switching from Psy Bass → Acid → Lead → Atmos → FX should feel that the instrument/workflow changed, not that a different vendor's plugin opened.

Pattern, phrase, piano-roll and arpeggiator editors are subordinate to the currently selected synth Part.

Vox Drums Engine and Mastering Engine remain separate products that share the VOX family design system.

Therefore:

> Global shell, interaction states, controls, graph grammar, spacing, typography and feedback remain VOX. Product and instrument identity is expressed through workflow, content, artwork and limited accent colour — without blurring product boundaries or turning editors into fake standalone instruments.
