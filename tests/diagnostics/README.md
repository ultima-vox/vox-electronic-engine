# Bass Render Diagnostics

## Overview

`bass_render_diagnostics` is an automated, host-independent headless diagnostic executable and regression harness for the Bass generation and audio rendering pipeline in **Vox Electronic Engine**.

It was introduced in response to Issue #45 to diagnose and verify:
1. **Empty MIDI export bug** ("generation appears not to work") — previously caused by `PluginProcessor::createPartMidiFile` reading uninitialized legacy part sequences instead of active pattern runtime sequences.
2. **NoteOff scheduling countdown bug** ("Bass sounds like a continuous beep") — previously caused by mid-block NoteOff countdown logic in `GeneratedNoteScheduler::processPpq` prematurely clearing pending note-offs or sending them out of order, leaving voices in an infinite sustain state.

## CLI Usage

```powershell
.\build\Release\bass_render_diagnostics.exe [OPTIONS]
```

### Options

| Flag | Description | Default |
|---|---|---|
| `--output-dir <path>` | Directory to write output audio WAV files, MIDI files, and `manifest.json`. | `build/bass-diagnostics` |
| `--scenario <name>` | Run only a specific scenario instead of all scenarios. | (Runs all scenarios) |
| `--help` | Display usage instructions and exit. | |

### Examples

Run all diagnostic scenarios and output into default folder:
```powershell
.\build\Release\bass_render_diagnostics.exe
```

Run only the transport stop/panic scenario to custom path:
```powershell
.\build\Release\bass_render_diagnostics.exe --output-dir temp/diag --scenario transport_start_stop_panic
```

## Scenarios

The suite covers 8 diagnostic suites (16 test scenarios total):

1. **`host_midi_lifecycle`**:
   - Sends short notes, sustained notes, NoteOffs, and repeated equal notes via Host MIDI.
   - Verifies 4 NoteOns and 4 NoteOffs, audio activity during notes, and silence/decay after release.
2. **`internal_generator_145bpm_c2`**:
   - Generates Bass pattern at 145 BPM, 4/4, C2 root (MIDI note 36, seed 42) using `psy-bass` profile.
   - Verifies valid dry audio output and finite samples.
3. **`profile_<profile>_seed_<seed>`**:
   - Tests `psy-bass`, `forest-bass`, and `dark-bass` across seeds `1234` and `98765`.
   - Verifies distinct pattern generation across different seeds and audible synth rendering.
4. **`rate_<rate>_block_<block>`**:
   - Tests sample rates (44.1 kHz, 48 kHz) and buffer block sizes (64, 512 samples).
   - Verifies sample-accurate rendering stability across sample rate and buffer size variations.
5. **`transport_start_stop_panic`**:
   - Exercises transport playback followed by transport stop, verifying decay to zero tail.
   - Exercises Panic CC 123 / all-notes-off request to ensure instantaneous release.
6. **`save_restore_rerender`**:
   - Saves processor state via `getStateInformation`, loads into a freshly constructed processor via `setStateInformation`, and renders identical transport blocks.
   - Compares peak amplitude, RMS energy, and waveform correlation.
7. **`routing_channels_audition`**:
   - Assigns Bass to MIDI channel 1. Verifies channel 3 input remains silent (< 1e-6 peak), channel 1 input renders audible audio, and slot audition mode renders properly.
8. **`midi_export_reimport_playback`**:
   - Generates a pattern, exports it via `PluginProcessor::createPartMidiFile`.
   - Validates that exported `.mid` file contains note events (> 0 NoteOns / NoteOffs).
   - Re-imports the exported MIDI into host playhead playback and verifies audible synth audio.

## Output Artifacts

All outputs are saved to the designated `--output-dir` (gitignored under `build/bass-diagnostics/`):
- `*.wav`: Rendered 16-bit PCM dry audio files.
- `*.mid`: Exported Standard MIDI Files (SMF Type 0/1).
- `manifest.json`: Machine-readable test execution report containing:
  - Scenario name and pass/fail status
  - Peak amplitude and RMS energy
  - Non-finite sample count
  - NoteOn / NoteOff event counts
  - Detailed diagnostic metrics
  - Relative paths to rendered WAV and MIDI files
