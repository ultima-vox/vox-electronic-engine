# VOX UI Family Architecture

**Status:** CANON

## Goal

All Ultima Vox audio products share one visual and interaction language while preserving product-specific workflows.

The shared layer is **VOX UI**. Individual plugins compose it rather than reimplementing their own themes.

## Product family

Initial consumers:

- Vox Electronic / Trance Engine;
- Vox Drums Engine;
- Mastering Engine;
- future Ultima Vox VST3/Standalone products.

These are separate products. Sharing VOX UI does **not** mean that product-specific instruments or workspaces are hosted inside another engine.

## Product boundary rule

### Vox Electronic / Trance Engine

Owns electronic/synth-oriented instruments and workflows such as:

- Psy Bass;
- Acid;
- Lead;
- Atmos / Texture;
- FX-oriented electronic sound modules;
- synth performance and sequencing tools.

**Arpeggiator, phrase and sequence editors are capabilities of a selected synth/Part, not standalone rack instruments.** They may appear as a panel on `Sound`, as a `Pattern` page, or as an expanded editor for the currently selected synth. They do not get their own identity slot, hero banner or separate engine identity.

### Vox Drums Engine

Is a separate plugin/engine. It owns drum-specific workflows such as:

- drum pads;
- kit/pad bank;
- sample waveform/editor;
- velocity editor;
- choke groups;
- per-pad routing;
- multi-row drum sequencer.

A Drums mock-up may be used as a family-style reference, but it is **not part of the Electronic Engine product specification** and must not be implemented as an Electronic Engine rack instrument.

### Mastering Engine

Is a separate plugin/engine. It owns:

- spectrum analyzer;
- loudness meter;
- true-peak meter;
- gain-reduction meter;
- stereo/correlation visualization;
- module chain;
- A/B and reference controls.

Mastering UI may be visually calmer and less accent-heavy, but still uses the same tokens and component grammar.

## Shared layer

The following are family-level and must remain common unless the design-system version changes:

- colors and semantic color roles;
- spacing grid and radii;
- typography scale;
- component states;
- `VoxLookAndFeel`;
- standard rotary controls;
- standard buttons, toggles and combo boxes;
- panels and section headers;
- graph visual language;
- meters visual language;
- keyboard-focus/hover/disabled behavior;
- scaling rules;
- tooltip and fine-adjustment conventions;
- realtime-safety boundary between UI and DSP.

Canonical details are defined in [`UI_DESIGN_SYSTEM.md`](UI_DESIGN_SYSTEM.md).

## Product-specific layer

Product identity is created by workflow, content hierarchy and specialized widgets, not by replacing the shared theme.

### Electronic / Trance Engine widgets

May include:

- instrument rack;
- synth sound panels;
- piano roll;
- per-synth arpeggiator/phrase editor;
- per-synth step/pattern sequencer;
- pattern generator;
- modulation matrix;
- sound/pattern/routing/zones/macros pages.

The sequencing widgets operate on the currently selected Part/instrument. They are never represented as a separate `Arp / Sequence` Part unless a future product requirement explicitly introduces such an instrument type.

### Vox Drums Engine widgets

May include:

- drum pads;
- sample waveform/editor;
- velocity editor;
- choke groups;
- per-pad routing;
- drum sequencer.

### Mastering Engine widgets

May include:

- spectrum analyzer;
- loudness meter;
- true-peak meter;
- gain-reduction meter;
- stereo/correlation visualization;
- module chain;
- A/B and reference controls.

## Source layout

Current bootstrap location in this repository:

```text
libs/vox-ui/
  CMakeLists.txt
  Tokens.h
  Typography.h
  VoxLookAndFeel.h/.cpp
  VoxComponents.h/.cpp
```

The module deliberately uses namespace:

```cpp
vox::ui
```

and must not depend on product DSP code.

## Long-term distribution

When a second plugin actively consumes the shared layer, move `libs/vox-ui` to a dedicated shared repository/library rather than maintaining copies.

Recommended target repository name:

```text
ultima-vox/vox-ui
```

Consumers should pin a known version/tag or commit. Do not track an unpinned moving branch in release builds.

Suggested versioning:

```text
v1.x  backward-compatible additions and visual refinements
v2.x  breaking component/API or design-language changes
```

## Dependency direction

Allowed:

```text
Product UI -> vox-ui -> JUCE GUI
```

Not allowed:

```text
vox-ui -> product DSP
vox-ui -> PluginProcessor
vox-ui -> presets/rack/instrument engines
DSP realtime path -> vox-ui
```

## Migration rule

Existing product UI does not need a big-bang rewrite.

Migrate in this order:

1. tokens;
2. LookAndFeel;
3. knob/button/combo primitives;
4. panels/headers;
5. graphs/meters;
6. product pages;
7. remove legacy local styling only after parity is confirmed.

Every migration step must preserve plugin state, automation IDs and DSP behavior.

## Acceptance criteria for a VOX-family plugin

A plugin belongs to the common UI family when:

- shared visual primitives come from VOX UI;
- no private duplicate palette exists;
- common controls have the same interaction semantics;
- resize/scaling works at the family-supported scale factors;
- specialized widgets visually derive from shared tokens;
- UI changes do not alter realtime DSP safety;
- product-specific deviations are documented rather than silently forked.
