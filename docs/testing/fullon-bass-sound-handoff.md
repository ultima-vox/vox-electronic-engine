# Full-on Bass SOUND integration handoff

This is a Bass-owned integration request, not an approved visual specification
or a working editor. Rack owns the shared editor, host gesture routing and
parameter pool. Bind one visible control to each canonical parameter ID below;
do not bind to the legacy `BassPanel` APVTS IDs. Labels, units, ranges and
defaults come from `fullOnParameters`; state v1 stores the same ordered values.

| Group | Control | Canonical ID | Plain range | Default |
| --- | --- | --- | --- | --- |
| Filter | Cutoff | `filter.cutoff` | 35–12000 Hz | 550 Hz |
| Filter | Resonance | `filter.resonance` | 0–1 | 0.55 |
| Pitch | Pitch amount | `pitch.amount` | 0–24 st | 9.6 st |
| Pitch | Pitch time | `pitch.time` | 1–100 ms | 18 ms |
| Amplitude | Attack | `amp.attack` | 0.1–100 ms | 0.8 ms |
| Amplitude | Decay | `amp.decay` | 1–500 ms | 60 ms |
| Amplitude | Sustain | `amp.sustain` | 0–1 | 0.5 |
| Amplitude | Release | `amp.release` | 1–250 ms | 28 ms |
| Character | Oscillator drive | `osc.drive` | 1–6 x | 2.6 x |
| Filter | Filter drive | `filter.drive` | 1–5 x | 2.28 x |
| Filter | Key tracking | `filter.keytracking` | 0–1 | 0.205 |
| Output | Output gain | `output.gain` | -60–6 dB | -2.85 dB |
| Pitch | Pitch curve | `pitch.curve` | 0.25–8 | 2 |

The Bass adapter proposes pool indices 0–12 in this same order; these are not
permission to renumber Rack host IDs. The generic editor must use the common
descriptor and host adapter for plain/normalized conversion, gestures,
automation, slot isolation and project restore. Bass parameters must not have
a second APVTS authority. ADSR and pitch duration are latched per NoteOn by
the existing engine; edits affect the next note. Output gain uses smoothing.

The historical #43 workspace contains additional preview-only controls. Their
presence does not establish approved independent DSP semantics. Owner must
identify the approved functional SOUND reference before this 13-control map
can be declared complete or additional controls can be implemented. No visual
or Cubase acceptance is claimed here.

No Bass-specific production visual asset bundle is currently identified on
this branch. Rack's approved VOX widget assets and tokens remain the source
for the shared editor; any Bass-specific artwork or layout asset must be named
by the approved reference rather than inferred from the preview workspace.

For content, Bass supplies its generator profiles and sound/pattern data; Rack's
versioned content API, scheduler, editing, export/reimport and signed `.vins`
packaging remain shared-owner responsibilities. A production handoff must use
one signed `.vins` and the approved graph or native exception, never a second
plugin, loader or sequencer.
