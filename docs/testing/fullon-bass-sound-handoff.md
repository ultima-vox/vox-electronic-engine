# Full-on Bass SOUND integration handoff

This is a Bass-owned integration request, not an approved visual specification
or a working editor. Rack owns the shared editor, host gesture routing and
parameter pool. Bind one visible control to each canonical parameter ID below;
do not bind to the legacy `BassPanel` APVTS IDs. Labels, units, ranges and
defaults come from `fullOnParameters`; state v1 stores the same ordered values.

`instruments/fullon-bass/sound.json` supplies the #89 schema-1 layout for the
current engine: CHARACTER, PITCH, FILTER, AMPLITUDE and OUTPUT. All 13 parameters
are `floating`, so every control is a `knob`. The layout intentionally contains
only arrangement/labels/IDs; ranges, units, defaults and values remain descriptor
authority. It does not establish the final Psy Bass interface or add DSP controls.

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

Historical visual reference: PR #43 at
`f2b105803d6001ca097914f1a041cd4f20c72cb9`,
`docs/UI_ACCEPTED_REFERENCE_RENDER_MAP.md`, Psy Bass `image-gen-1(1).png`.
The corresponding workspace's controls were explicitly partly preview-only.
These 20 labels lack independent endpoints in the current FullOnBass descriptor:
Octave, Semitone, Blend, Phase, Filter Env Amount, Tone, Presence, Mix,
Accent Amount, Vel>Cutoff, Vel>Drive, Accent Tone, Portamento, Unison, Detune,
Spread, Modulation Amount/Attack/Decay/Release. This is an inventory for owner
decision, not a request to add all of them. Internal glide and fixed oscillator
phase/blend behavior already exist; that does not make them host parameters.
The historical single Drive label also needs a decision between today's
`osc.drive` and `filter.drive`. Preview modulation routes/displays have no
approved typed data source. The approved current functional matrix remains open.

## Canonical host validation and packaging handoff

Use the unchanged #89 checkout at
`a263efc6464ffc521b7b847e127bc2b7c61bc414`, with Bass-owned test injection:

```powershell
cmake -S <rack-source> -B <rack-build> -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PROJECT_INCLUDE=<bass-source>/tests/bass/module/BassRackTestInjection.cmake `
  -DVOX_BASS_RACK_TEST_SOURCE=<bass-source>/tests/bass/module/BassNativeRackTests.cpp `
  -DVOX_BASS_SOUND_TEST_SOURCE=<bass-source>/tests/bass/module/BassSoundLayoutTests.cpp
cmake --build <rack-build> --config Release --target bass_sound_layout_tests --parallel 2
& <rack-build>/Release/bass_sound_layout_tests.exe <absolute-DLL-path> <bass-source>/instruments/fullon-bass/sound.json
```

This checks the actual DLL's imported descriptor against all 13 source
IDs/types/units/ranges/defaults/pool indices, then runs the unchanged host parser
and coverage validator. It rejects missing, duplicate and wrongly typed controls.
It is not an editor/gesture test. Existing processor tests cover host parameter
edits and state/render restoration; actual SOUND gestures, DAW automation and
project reopen remain required on Claude's assembled package/editor.

Local Windows x64 Release validation passed against the DLL downloaded from
the green exact-head `f7a9636` run
[41011113634](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/41011113634).
Both Windows jobs passed. DLL artifact SHA256:
`9CD8BB226D74A9087500F33770EDF2E81E9B3D72324B4D231FD74CCC49E63BF6`.
The current branch adds the same canonical parser test and layout artifact to
the Bass CI job; its new exact-head result must be checked separately.

Claude should list `instruments/fullon-bass/sound.json` in the package's hashed
`files` and map manifest `ui["com.ultimavox.fullon-bass"]` to that member (or its
chosen archive path). The shared packer supplies hashes and native signatures.
No private packer, loader, trust key or production package is supplied here.

## Provisional playing-policy audio

Set `VOX_BASS_PLAYING_WAV_DIR` before running `fullon_bass_parameter_tests`.
CI includes the result under `VoxFullOnBass-DSP-Evidence/playing-policy`.
Each scenario has a two-second, mono float WAV and CSV of absolute sample
positions plus decimal MIDI status/data bytes. Settings: 48 kHz, 256 samples,
all descriptor defaults except release at 1 ms (`-short`) or 250 ms (`-long`).
All events use channel 1. NoteOn velocity is 110 for MIDI note 36, 100 for note
43, and 80 for the same-key retrigger. No FX, normalization or DAW routing.

- `overlap`: 36 on at 0; 43 on at 9600; 43 off at 19200 retriggers held 36;
  36 off at 28800 releases.
- `sustain`: pedal down at 4800; 43 on/off at 9600/14400; pedal up at 24000
  returns to held 36; 36 off at 33600 releases.
- `same-key-retrigger`: 36 on again at 9600; one off at 19200 releases it.
- `note-off`: 36 off at 19200 releases naturally.
- `cc120`: immediate stop at 19200; new 43 on/off at 38400/48000 proves recovery.
- `cc123`: pedal down at 4800, 36 off at 9600 stays sustained; CC123 at 19200
  clears sustain and releases naturally; new 43 on/off at 38400/48000 recovers.

Owner must approve last-held-note retrigger versus another mono policy.
Existing single-note NoteOff/CC123 reference WAVs remain the comparison oracle.
Loop/seek/stop/reopen require the assembled Rack's transport/reset path and
are not simulated by these DSP-only examples. Cubase beep reproduction remains
open until the same production-configured Rack Release binary is rendered with
recorded project/events, routing, sample rate, buffer and envelope settings.

No Bass-specific production visual asset bundle is currently identified on
this branch. Rack's approved VOX widget assets and tokens remain the source
for the shared editor; any Bass-specific artwork or layout asset must be named
by the approved reference rather than inferred from the preview workspace.

For content, Bass supplies its generator profiles and sound/pattern data; Rack's
versioned content API, scheduler, editing, export/reimport and signed `.vins`
packaging remain shared-owner responsibilities. A production handoff must use
one signed `.vins` and the approved graph or native exception, never a second
plugin, loader or sequencer.
