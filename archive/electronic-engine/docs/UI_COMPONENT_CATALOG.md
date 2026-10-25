# VOX UI — Component Catalog v1.1

**Status:** CANON candidate  
**Scope:** conceptual UI elements for the Ultima Vox audio-plugin family  
**Applies to:** Vox Electronic/Trance Engine, Vox Drums Engine, Mastering Engine, future VOX audio products

This catalog defines **types of UI elements**, not every repeated instance of a control.

The visual rules, colours, spacing, typography and interaction conventions are defined in `UI_DESIGN_SYSTEM.md`.

---

# 1. Ownership model

The VOX family shares UI primitives but keeps product workflows separate.

```text
VOX UI CORE
├── shared controls
├── shared graph language
├── shared meters
├── shared layout/state grammar
└── shared interaction rules

VOX ELECTRONIC ENGINE
├── synth Parts
├── per-synth Pattern/Arp workflows
├── FX workflows
├── modulation/routing/zones
└── keyboard/performance

VOX DRUMS ENGINE
├── kits / pads
├── sample editing
├── drum sequencing
└── drum routing/mixer

VOX MASTERING ENGINE
├── mastering module chain
├── analysis
├── loudness / true peak / GR
└── A/B reference workflows
```

**Important:** a shared component does not imply shared product ownership.

---

# 2. Component levels

## Level A — primitives

Reusable family-wide controls:

- `VoxKnob`;
- `VoxLinearSlider`;
- `VoxFader`;
- `VoxButton`;
- `VoxIconButton`;
- `VoxToggle`;
- `VoxSwitch`;
- `VoxSegmentedControl`;
- `VoxComboBox`;
- `VoxTextInput`;
- `VoxValueField`;
- `VoxLabel`;
- `VoxSectionHeader`;
- `VoxTabBar`;
- `VoxStatusIndicator`;
- `VoxTooltip`;
- `VoxScrollBar`;
- `VoxContextMenu`;
- `VoxDivider`;
- `VoxPanel`.

These belong in shared `vox-ui`.

## Level B — audio primitives

Reusable audio-oriented controls/visualizations:

- `VoxWaveform`;
- `VoxSpectrumAnalyzer` base;
- `VoxMeter` / `VoxLevelMeter`;
- `VoxEnvelopeEditor`;
- `VoxFilterResponse`;
- `VoxXYPad`;
- `VoxModulationGraph`;
- `VoxKeyboard`;
- `VoxMacroControl`;
- `VoxModulationRing`;
- `VoxStereoScope`;
- `VoxCorrelationMeter`;
- `VoxTransferCurve`.

These are shared when the semantics are generic.

## Level C — workflow widgets

Complex components composed from primitives:

- preset browser;
- instrument rack;
- mixer strip;
- piano roll;
- step sequencer;
- arpeggiator;
- Pattern editor;
- modulation matrix;
- effect chain;
- routing matrix;
- zone editor;
- sample editor;
- drum pad;
- mastering module chain;
- analyzer workspace.

Ownership is product-specific even when implementation primitives are shared.

## Level D — pages/workspaces

Examples:

- Sound;
- Pattern;
- Routing;
- Zones;
- Macros;
- Advanced;
- FX;
- Mixer;
- Mastering Chain;
- Analyzer;
- Drum Kit;
- Sample Edit.

Pages compose existing components and must not invent private visual styles.

---

# 3. Shared shell components

## 3.1 VoxGlobalHeader

Purpose:

- product branding;
- preset/project selector;
- previous/next;
- save;
- global output;
- MIDI activity;
- CPU/load status;
- Panic;
- Settings;
- optional seed/random control.

## 3.2 VoxMainNavigation

Family navigation container. Labels are product-specific, geometry and interaction grammar are shared.

## 3.3 VoxPresetSelector / VoxPresetBrowser

Supports:

- Factory/User source;
- search;
- category/filter;
- load/save;
- rename/delete where allowed;
- dirty state;
- read-only state;
- full/sound preset distinction where supported.

---

# 4. Electronic Engine — Instrument / Part system

## 4.1 VoxInstrumentRack

Container for Electronic Engine Parts.

Responsibilities:

- selection;
- ordering;
- enable;
- mute;
- solo;
- lock;
- MIDI channel;
- level/pan;
- output destination;
- module resolution/error state.

## 4.2 VoxInstrumentSlot

Displays:

- slot number;
- instrument identity;
- name;
- subtitle/preset;
- MIDI channel;
- power/activity;
- selected/muted/solo states.

## 4.3 VoxPartHeader

Header of selected Part:

- instrument name;
- preset;
- MIDI channel;
- enable/mute/solo/lock;
- level/pan/output where applicable.

**Arpeggiator and sequence editors are never represented by `VoxInstrumentSlot` on their own.**

---

# 5. Generic parameter controls

## 5.1 VoxKnob

Variants:

- Small;
- Normal;
- Large.

Capabilities:

- current value;
- default value;
- unit;
- modulation amount;
- automation/modulation state;
- reset;
- fine adjustment;
- tooltip.

## 5.2 VoxLinearSlider / VoxFader

`VoxFader` adds:

- dB scale;
- unity marker;
- optional integrated meter.

## 5.3 VoxChoiceControl

Used for:

- waveform;
- filter type;
- sync division;
- routing destination;
- MIDI mode;
- modulation source/destination.

Visual forms:

- ComboBox;
- segmented switch;
- compact popup selector.

---

# 6. Shared synthesizer components

Used by Psy Bass, Lead, Acid, Atmos and future synth Parts.

## 6.1 VoxOscillatorPanel

Possible controls:

- waveform;
- morph;
- tune/octave/semitone/fine;
- phase;
- sync;
- FM;
- ring modulation;
- noise;
- unison;
- voices;
- detune;
- spread;
- oscillator mix.

Only controls supported by the current engine are shown.

## 6.2 VoxWaveformDisplay / VoxWaveformSelector

Modes may include:

- generated oscillator waveform;
- wavetable position;
- sample waveform where appropriate.

No fake realtime animation.

## 6.3 VoxFilterPanel / VoxFilterResponse

Contains/supports:

- filter type;
- cutoff;
- resonance;
- drive;
- key tracking;
- envelope amount;
- slope.

Response graph follows actual state.

## 6.4 VoxEnvelopeEditor

Generic envelope for:

- amplitude;
- filter;
- pitch;
- modulation.

Minimum ADSR, optionally delay/hold/curve/loop if implemented.

## 6.5 VoxPitchEnvelope

Compact pitch/transient envelope for bass, kick/percussive synths and FX.

## 6.6 VoxUnisonPanel

- voices;
- detune;
- spread;
- phase/randomisation.

## 6.7 VoxDrivePanel

- drive/amount;
- character/type;
- mix;
- optional output compensation.

---

# 7. Electronic Engine synth workflows

## 7.1 VoxBassPanel

Composition may include:

- oscillator;
- amp envelope;
- pitch envelope;
- filter;
- drive;
- accent;
- glide/performance;
- output;
- modulation.

## 7.2 VoxLeadPanel

Composition may include:

- oscillator/morph;
- sync/FM/ring/noise;
- unison;
- filter;
- amp envelope;
- modulation envelope;
- glide;
- macros;
- embedded Arpeggiator/Phrase panel;
- performance;
- modulation matrix.

## 7.3 VoxAcidPanel

Must expose supported Acid semantics:

- waveform;
- cutoff;
- resonance;
- envelope amount;
- decay;
- accent;
- slide;
- drive;
- output;
- integrated Acid sequence workflow.

## 7.4 VoxAtmosPanel

Possible semantic controls:

- layer A/B;
- sample/granular parameters;
- texture;
- blend;
- brightness;
- motion/evolution;
- envelopes;
- spectral cloud;
- space/shimmer/reverb;
- XY morph;
- spread;
- output.

## 7.5 VoxSemanticFxPanel

Event families may include:

- sweep;
- laser;
- riser;
- downlifter;
- impact;
- whoosh;
- zap;
- noise burst;
- metallic;
- alien.

Controls may include:

- family;
- tone;
- duration;
- brightness;
- motion;
- texture;
- drive;
- output.

---

# 8. Synth sequencing and musical generation

This section is a **capability layer of a selected synth Part**, not a standalone Electronic Engine instrument.

## 8.1 Ownership rule

Every sequence, arp, phrase, piano-roll and generated pattern is owned by the selected Part/instrument.

Examples:

```text
Lead Part
├── Sound
│   └── compact Arpeggiator / Phrase panel
└── Pattern
    └── expanded VoxPatternEditor

Acid Part
├── Sound
│   └── integrated Acid Step Sequencer
└── Pattern
    └── expanded Acid sequence editing
```

Forbidden:

- `Arp / Sequence` as a standalone rack Part;
- independent hero identity for Arp;
- sequencing state detached from the selected synth;
- one generic sequence silently controlling a different Part.

## 8.2 VoxPatternEditor

Expanded editor container for the currently selected synth.

It composes:

- `VoxStepSequencer` and/or `VoxPianoRoll`;
- `VoxArpeggiator` controls when supported;
- lane selector;
- Pattern preset selector;
- transport/playhead;
- generator/mutate controls;
- per-pattern routing/modulation where applicable.

## 8.3 VoxStepSequencer

Must support up to the engine's sequence limit.

Possible lanes:

- note;
- gate;
- velocity;
- accent;
- probability;
- ratchet;
- slide;
- tie;
- octave;
- gate width;
- modulation lanes.

Only supported lanes are shown for a given instrument.

## 8.4 VoxPianoRoll

Must provide:

- note blocks;
- start/duration/pitch;
- velocity;
- selection/move/resize/delete;
- grid snapping;
- zoom/scroll;
- playhead;
- keyboard gutter.

Optional lower lanes:

- velocity;
- probability;
- accent;
- ratchet;
- slide;
- modulation.

## 8.5 VoxArpeggiator

Embedded synth capability.

Sections may include:

**Mode**
- Up;
- Down;
- Up/Down;
- Down/Up;
- As Played;
- Random;
- Chord.

**Timing**
- rate/division;
- gate;
- swing;
- sync/free.

**Pitch**
- octave range;
- transpose;
- scale/key constraint.

**Pattern**
- length;
- step enable;
- octave offset;
- velocity;
- tie;
- accent;
- probability.

**Playback**
- latch/hold;
- retrigger;
- restart behaviour.

No unsupported parameter may appear as a fake control.

## 8.6 VoxPatternGenerator

- style/profile;
- seed;
- generate/regenerate;
- mutate selected;
- clear selected;
- target = current Part by default;
- deterministic status.

## 8.7 VoxMidiSourceSelector

Canonical modes:

- AUTO;
- PIANO ROLL;
- GENERATOR;
- BOTH.

Order must remain aligned with the engine/host parameter schema.

---

# 9. Modulation

## 9.1 VoxMacroPanel

Per Part:

- 8 macros where supported;
- label;
- value;
- assignment;
- automation/modulation state.

## 9.2 VoxLfoEditor

- waveform;
- rate;
- sync/free;
- phase;
- retrigger;
- polarity;
- amount;
- optional shape/skew.

## 9.3 VoxStepModulator

- number of steps;
- values;
- rate;
- smoothing;
- bipolar/unipolar;
- randomise/rotate/reset.

## 9.4 VoxModulationMatrix

Rows:

- source;
- optional transform/polarity;
- destination;
- depth;
- enable/delete.

Destination choices come from the instrument contract/descriptor.

---

# 10. Keyboard and performance

## VoxKeyboard

- mouse note input;
- pressed-note state;
- octave labels;
- MIDI activity.

Related controls:

- `VoxPitchBend`;
- `VoxModWheel`;
- `VoxXYPad`;
- Chords/Scale controls where supported.

---

# 11. Routing and zones

## VoxRoutingPanel

Per Part:

- route mode;
- MIDI channel;
- input mode;
- output destination;
- transpose;
- layer/channel routing.

## VoxZoneRangeEditor

- key low/high;
- velocity low/high;
- clear range handles and active range.

## VoxRoutingMatrix

For more complex source/destination routing.

---

# 12. Mixer and meters

## VoxMixerStrip

- name;
- activity;
- mute;
- solo;
- pan;
- fader;
- meter;
- output destination.

## VoxLevelMeter

Variants:

- mono/stereo;
- peak;
- RMS/average where relevant.

Clip/warning states must be explicit.

---

# 13. Effects

Electronic Engine effect workflow may include:

- distortion;
- wavefolder;
- phaser;
- flanger;
- chorus;
- bitcrusher;
- delay;
- reverb.

## VoxEffectSlot

- effect identity;
- enable/bypass;
- mix;
- selected state;
- drag/reorder where supported.

## VoxEffectChain

Shows signal order, bypass and serial/parallel structure where implemented.

Specialized graph components may include:

- delay timing visualization;
- reverb decay visualization;
- distortion transfer curve.

---

# 14. Vox Drums Engine — separate product catalog

Drum-specific components belong to **Vox Drums Engine**, not Electronic Engine.

They remain valid VOX-family concepts and should use shared `vox-ui` primitives.

## 14.1 VoxDrumPad / VoxDrumGrid

Displays:

- pad identity;
- sample/instrument name;
- activity/velocity;
- mute/solo;
- selection.

## 14.2 VoxSampleEditor

Editable waveform workspace:

- start/end;
- trim;
- loop;
- fades;
- zoom/scroll;
- transient markers;
- playhead.

## 14.3 VoxDrumVoiceEditor

Per pad:

- sample;
- pitch;
- envelope;
- filter;
- transient;
- pan;
- level;
- output;
- choke group.

## 14.4 VoxDrumSequencer

Rows correspond to drum voices/pads.

May expose:

- gate;
- velocity;
- probability;
- mute/solo;
- output;
- fill/humanize.

These components must not appear as an Electronic Engine rack instrument.

---

# 15. Mastering Engine — separate product catalog

## VoxModuleChain

Ordered mastering processors with selected/enabled/bypass/unavailable states.

## VoxSpectrumWorkspace

May contain:

- input trace;
- output trace;
- reference trace;
- EQ response overlay.

## VoxLoudnessMeter

Only implemented metrics may be shown, such as:

- Momentary;
- Short-Term;
- Integrated;
- Loudness Range.

## VoxTruePeakMeter

Dedicated true-peak display.

## VoxGainReductionMeter

For compressor/limiter/dynamics.

## VoxCorrelationMeter / VoxStereoScope

Stereo phase/image visualization.

## VoxReferencePanel / VoxBeforeAfterControl

- A/B;
- reference source;
- level matching;
- bypass;
- compare state.

---

# 16. Data truth rule

Visual components must reflect actual engine state.

Forbidden:

- fake waveform animation;
- fake filter curves;
- fake spectrum;
- fake meters;
- controls with no backing parameter;
- visual modulation not tied to a real route;
- decorative playheads not synchronized to transport.

If data is unavailable, show an explicit unavailable/empty state.

---

# 17. Realtime boundary

No complex visualization may read mutable DSP structures directly.

```text
DSP/audio thread
    ↓ lock-free/atomic snapshot
UI bridge
    ↓ timer/message thread
visual component
```

UI repaint rate is independent from audio block rate.

---

# 18. Shared vs product-specific implementation

## Shared `vox-ui`

Should contain:

- tokens;
- typography;
- `VoxLookAndFeel`;
- generic controls;
- graph/meter bases;
- envelope/filter response;
- keyboard base;
- mixer primitives;
- shared interaction grammar.

## Electronic Engine product layer

Owns compositions for:

- Psy Bass;
- Lead;
- Acid;
- Atmos;
- Semantic FX / FX workflows;
- Instrument Rack;
- Pattern Editor;
- Piano Roll;
- per-synth Arpeggiator;
- per-synth Step Sequencer;
- Pattern Generator;
- Modulation Matrix;
- Routing/Zones.

## Drums product layer

Owns:

- Drum Pad/Grid;
- Drum Voice Editor;
- Sample Editor composition;
- Choke Groups;
- Drum Sequencer.

## Mastering product layer

Owns:

- Module Chain;
- Loudness workflow;
- True Peak workflow;
- reference/A-B workflow;
- mastering analyzer workspace.

---

# 19. Implementation rule

A complex widget is composed rather than painted as one monolithic component.

Example:

```text
VoxLeadPanel
├── VoxOscillatorPanel
├── VoxFilterPanel
├── VoxEnvelopeEditor
├── VoxUnisonPanel
├── VoxArpeggiator        ← capability of Lead
├── VoxMacroPanel
└── VoxModulationMatrix
```

Expanded editing:

```text
Lead Part / PATTERN
└── VoxPatternEditor
    ├── VoxPianoRoll or VoxStepSequencer
    ├── VoxArpeggiator controls
    ├── lanes
    ├── Pattern preset
    └── playhead/generator tools
```

---

# 20. Definition of Done

A component is complete only when:

1. purpose is defined;
2. ownership is defined: shared vs product-specific;
3. backing data/parameters are identified;
4. interactions/states are defined;
5. scaling works;
6. visual tokens come from VOX UI;
7. realtime safety is preserved;
8. empty/error states exist where required;
9. it has no fake functionality.

---

## Canonical rule

**Design tokens → primitive → audio primitive → workflow widget → page → product.**

For Electronic Engine specifically:

> Arp/sequence/piano-roll/pattern tools belong to the selected synth Part. Drums belongs to Vox Drums Engine. Shared appearance does not erase product or state ownership boundaries.
