export const sections = ['Overview','Tokens','Showcase','Registry','Drums','FX','Binding','Plan'] as const;
export type Section = typeof sections[number];
export const registry = [
  ['Rotary', 'Knob · Bipolar · Encoder · Macro', 'controls.tsx', 'Working · parameter gestures'],
  ['Linear', 'Vertical Fader · Slider · Range · Numeric Value', 'controls.tsx', 'Working · base value / default'],
  ['Performance', 'Drum Pad · Pad Grid / List · Keyboard · Trigger · XY Pad', 'perform.tsx', 'Working · audible only in Drums'],
  ['Graphs', 'Waveform · Envelope · Filter Response · Curve Editor', 'graph.tsx', 'Working · source preview labels'],
  ['Telemetry', 'Peak · RMS · OVER · MIDI / event status', 'controls.tsx + adapters', 'Measured browser audio only in Drums'],
  ['Actions', 'Button · Toggle · Switch · Segmented · Dropdown', 'ui.tsx', 'Working · keyboard focus'],
  ['Navigation', 'Tabs · Panel · Splitter · Inspector · Browser', 'ui.tsx + adapters', 'Working · no native docking claim'],
  ['Musical', 'Step Grid · Parameter Lane · Song Timeline · Modulation Matrix', 'perform.tsx + content.tsx', 'Working · typed Program / Pattern'],
  ['Content', 'Preset Browser · Search · Favorites · Drop Zone', 'content.tsx', 'Working · local payloads only'],
] as const;
export const drumsMatrix = [
  ['#127 §1', 'Resizable workstation, two side zones, one Mixer', 'Browser', 'Native placement and Windows DPI not tested'],
  ['#127 §2', 'Separate pad selection and audition; pinned contexts', 'Browser', 'No processor or host MIDI binding'],
  ['#127 §3', '16 × 16/32/64, step values, Song source vs first-edit copy', 'Browser', 'Browser MIDI file export; no DAW drag'],
  ['#127 §3', 'Mixer, real browser meters and Kit Level across all buses', 'Browser', 'Native enable/fallback and returns not verified'],
  ['#127 §3', 'Generator and 23 styles', 'Partial', 'Names/settings + generic fixture; genre engine unattached'],
  ['#127 §3', 'Library / samples / routing / diagnostics', 'Partial', 'Local snapshots / synthetic donor samples / monitoring; no native factory content or host'],
  ['#127 §6–7', 'Native functional and owner visual acceptance', 'Pending', 'Windows, VST3 validator, Cubase / REAPER and visual sign-off'],
] as const;
export const fxMatrix = [
  ['FX §2–3', '4 layers, 20 curves, 4 stages, 8 macros, 64 typed routes', 'Browser', 'UI Program configuration, not synth algorithms'],
  ['FX §3, §5', '8 overlapping events, 64 grains total', 'Partial', '8 simulated UI events capped; no grain allocator'],
  ['FX §5–6', 'Trigger pattern and seeded Generate', 'Browser', 'Visual clock / typed data only; no native MIDI or audible render'],
  ['FX §7', 'Eight focused work areas, presets, A/B and diagnostics', 'Browser', 'No fake meter, CPU, clip or export fidelity'],
  ['FX §8–10', 'Stable host map, immutable publication, validator & DAW', 'Pending', 'Separate native engineering and audio acceptance'],
] as const;
export const bindings = [
  ['Level · Pad 01', 'demo.pad.1.level', 'drm.p01.level', '0–1 linear, UI % · browser default .85'],
  ['Tune · Pad 01', 'demo.pad.1.tune', 'drm.p01.tune', '−24…+24 semitones · bipolar'],
  ['Decay · Pad 01', 'demo.pad.1.decay', 'drm.p01.decay', '.01–10 s logarithmic · default .38 s'],
  ['Kit Level', 'browser-kit-level', 'drm.kit.volume', 'All outputs · not a post-Main gain'],
  ['Step and Song', 'PatternRef + Step / Bar', 'PatternStore / Song model', 'Structural transaction; first actual edit creates a bar copy'],
  ['FX time curves / routes', 'Program v1', 'Native Program controller (target)', 'Not flattened into host points; see audited FX IDs in DS-00'],
  ['Meters', 'Web Audio snapshot / unavailable', 'takePadMeter / takeMainMeter', 'One coordinator; no UI writes to DSP'],
] as const;
