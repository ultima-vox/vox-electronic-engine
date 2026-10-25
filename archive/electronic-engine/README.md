# Vox Electronic Engine

Generative VST3 instrument for **dark psytrance, psytrance, forest and related electronic music**.

## Status

Early vertical slice (0.1.x). The current target is a reliable VST3 instrument in Cubase before expanding into a larger generative sound-design system.

### Implemented

- JUCE 9.0.1 + CMake project
- VST3 instrument target for Windows
- Host-tempo-aware 16-step rolling bass sequencer
- Deterministic dark-psy pattern generator
- Dedicated psy-bass synth voice with phase reset and fast pitch envelope
- Automatable Drive and Release parameters
- Plug-in state save/restore
- Generic 16-slot Rack with stable SlotId, CH1-CH16/OFF routing, Layer,
  key/velocity zones, transpose, mute/solo/lock, level/pan and 16x8 host macros
- Built-in Bass, Acid, Lead, Semantic FX and Atmos modules through one provider
  contract; module selector, sound presets and per-slot deterministic patterns
- Per-slot shared modulation routes with LFO 1/2, Envelope, Step Mod,
  Sample & Hold, Random, Velocity, mod wheel, aftertouch and canonical restore
- Post-rack fixed internal FX chain: distortion, wavefolder, phaser, flanger,
  chorus, bitcrusher, delay and reverb; bypass and Full/project state restore
- Global provider-driven Generate All / Mutate All orchestration; locked slots
  remain byte-identical, and Mutate Selected preserves unselected steps
- Missing/incompatible module recovery without silent instrument substitution
- Versioned deterministic Rack/project state and concurrent host-state snapshots
- Native JUCE editor with selected-slot audition and capability-driven controls
- 25 automated module, state, routing, realtime-isolation and UI tests
- Windows x64 VST3 plus plugin-only CPack ZIP; optional NSIS installer

## Build on Windows

Requirements:

- Visual Studio 2022 with Desktop development with C++
- CMake 3.22+
- Git

```powershell
git clone https://github.com/ultima-vox/vst-engine.git
cd vst-engine
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Release --target package
```

VST3 bundle and compact release package are produced under:

```text
build/VstEngine_artefacts/Release/VST3/
build/Vox Electronic Engine-0.1.0-win64.zip
```

For Cubase, copy the resulting .vst3 bundle to the standard system VST3 location, typically:

```text
C:\\Program Files\\Common Files\\VST3
```

## Architecture direction

Modular architecture per issue #11 (independent CMake targets):

```text
apps/vst3/      PluginProcessor (thin VST3/APVTS host adapter), PluginEditor (composition only)
libs/core/      shared stable IDs/constants          -> vst_core
libs/instrument/versioned ABI, provider/registry, host macro contract -> vst_instrument
libs/sequence/  canonical Sequence/Step/timing model -> vst_sequence
libs/transport/ pure PPQ/grid musical-time math      -> vst_transport
libs/midi/      source-mode policy, MIDI export, generated-note scheduler -> vst_midi
libs/bass/      psy-bass DSP voice                   -> vst_bass
libs/acid/      303-style instrument/generator       -> vst_acid
libs/lead/      polyphonic wavetable/FM lead          -> vst_lead
libs/semanticfx/semantic transition instruments      -> vst_semantic_fx
libs/atmos/     evolving texture instrument           -> vst_atmos
libs/modulation/shared bounded modulation matrix      -> vst_modulation
libs/effects/   fixed bounded internal FX chain       -> vst_effects
libs/preset/    preset format/filesystem/migration   -> vst_preset
libs/generator/ seed-based pattern generation        -> vst_generator
libs/ui/        reusable JUCE components             -> vst_ui
tests/          one test executable per module + tests/integration/
```

Dependency rule: lower-level modules -> shell. The shell (PluginProcessor/PluginEditor)
links the module libraries; lower modules never depend on the shell.

Implemented engines:

1. Psy bass
2. Acid / resonant sequence engine
3. FM/wavetable dark leads
4. Semantic FX generator
5. Forest/alien Atmos texture generator
6. Fixed internal FX chain and shared modulation matrix
7. MIDI-out mode for external instruments

## Design rule

Generation must remain **musically constrained**. Randomization is not allowed to become unconstrained noise: groove, scale, phrase structure and genre-specific rhythm rules are explicit parts of the generator.


## MIDI / Cubase workflow

VST Engine supports four MIDI source modes:

- **AUTO** — if the host sends note events, the piano-roll MIDI is used; otherwise the internal generator is used.
- **PIANO ROLL** — only incoming host MIDI is used.
- **GENERATOR** — only VST Engine's generated MIDI is used.
- **BOTH** — incoming host MIDI and generated MIDI are combined.

The generated stream can be assigned to MIDI channels 1-16. This is the routing foundation for future multitimbral Parts.

### Recording generated MIDI into Cubase

A standard VST3 plug-in does not directly modify an existing Cubase piano-roll part. Generated notes are exposed as VST3 MIDI output so the host can route/record them to a MIDI track. Once recorded, switch VST Engine to **PIANO ROLL** or **AUTO** and edit the notes normally in Cubase.

Planned next step: drag-and-drop MIDI clip export from the plug-in UI into the Cubase project.


### Drag generated MIDI into Cubase

The editor exposes **DRAG MIDI TO CUBASE**. Dragging it creates a Standard MIDI File from the current 16-step generator pattern and starts an external file drag operation.

The exported clip uses:
- the current generator pattern
- the selected root note
- the selected generator MIDI channel
- 960 PPQ resolution
- 16th-note step spacing

Cubase can then place/import the MIDI clip into the project for normal piano-roll editing.

## Checkpoint 7A product split

Kick and MATCH are no longer active Vox Electronic Engine modules. Their DSP/UI
snapshots live under `legacy/vox_drum_engine` and `legacy/mastering_engine` for
future extraction. They are not compiled, registered, routed, rendered or
listed as factory content by this product.

Legacy Kick/MATCH APVTS IDs and serialized Part payloads remain readable for
project migration. Restoring old state cannot reactivate Kick audio. Checkpoint
7B converts removed-module payloads into explicit unresolved slot state.

## Editor and presets

Editor uses compact BASS, SEQ, PRESETS and SETTINGS tabs at 1180x760,
resizable from 1040x680 to 1600x1000. PRESETS reads one typed catalog from
`PresetManager`: factory Bass presets are read-only; user Bass Sound presets
and Full presets support save, load, rename, delete and refresh.
Files live under JUCE user application data in `UltimaVox/VST-Engine/Presets/User`.
Preset schema remains v1 and legacy v1 Sound presets remain loadable.

## Host synchronization

Generated playback is synchronized to the host's musical position:

- **PPQ sync (primary).** When the host provides an `AudioPlayHead::PositionInfo`
  PPQ position (Cubase does), step onsets are scheduled from the absolute
  quarter-note position against the canonical `stepsPerQuarterNote()` grid.
  This gives bar-accurate alignment at any start locator, immediate realign on
  seek/jump, correct wrap on cycle/loop, and musical alignment that survives
  BPM changes. The alignment math lives in `libs/transport/PpqSync.h` and is
  unit-tested (`transport_sync_tests`).
- **Fallback.** If a host does not provide PPQ, playback falls back to the
  historical BPM-derived free-running step timer (start at step 0, no locator
  alignment). The timing basis (canonical step durations) is covered by the
  sync tests; the fallback itself requires a Cubase manual check.

Probability still advances exactly once per sequence step and reseeds on every
transport restart, so a given project state restarted from the same point
reproduces the same decisions as MIDI export.

## MIDI source isolation

The generator decision (`libs/midi/SourceSelector.h`, unit-tested by
`midi_source_tests`) only ever observes **external host notes** and **GUI
keyboard notes**:

- **AUTO** — host/GUI input wins; the generator runs only when neither is present.
- **PIANO ROLL** — host/GUI input only.
- **GENERATOR** — only generated material (incoming host MIDI is dropped).
- **BOTH** — host/GUI input and generated material are combined.

Internally generated MIDI is never counted as user input. The virtual keyboard
state deliberately sees only what the user plays on it, so "generated notes
would highlight the keyboard" can never suppress generator playback in AUTO.
Consequence: the on-screen keyboard currently highlights only GUI-played keys,
not Cubase incoming or generated notes; Cubase piano-roll input still plays
normally.

Host/Cubase-dependent behavior (actual PPQ alignment, locator start, seek,
loop wrap, AUTO source switching in a real session) must be verified in Cubase
per the acceptance checklist.
