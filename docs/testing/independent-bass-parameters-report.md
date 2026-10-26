# Independent Bass parameter core

Date: 26 October 2026. Base source tree:
`6e4fd4df5232b12c41bb2d436761aa4f375e3f4c` (main after PR #51).

## Scope and owner correction

One ordinary knob changes one parameter. The owner confirms no POC projects
need migration and rejects a legacy compatibility mode. Product/contract docs
record that intentional replacement. This change supplies the independent DSP
model; host automation/UI replacement is still required before completion.

`FullOnBassInstance` has 13 named parameters with real units/ranges, a canonical
52-byte little-endian payload and validate-before-commit loading. Shared voice
velocity/output gain follow nonlinear processing. Gain uses JUCE SmoothedValue;
key tracking participates in filter coefficient invalidation. ADSR and pitch
duration are note-on snapshots, following the pinned JUCE ADSR contract. No
live ADSR setParameters or compatibility-mode switches remain.

## Observed evidence

Final `fullon_bass_parameter_tests`, manually linked against real pinned JUCE
9.0.1 modules, GCC 13.3, project code ASan/UBSan (`-O0 -g`):

- **3625 focused checks PASS**: every setter changes only its own payload word;
  clamping/nonfinite/unknown inputs, invalid state rollback, empty default reset,
  canonical round trip, actual rendered gain/velocity scaling, next-note ADSR,
  live key tracking, gain ramp, MIDI validation, lifecycle and silence.
- **215 common instrument compliance checks PASS**, using a test factory adapter
  that creates the actual new DSP instance, not an audio mock.
- Rates 44.1/48/96 kHz and blocks 64/256/1024 in audio-independence checks;
  common compliance adds 128/512; 192 kHz prepare/tail-budget check.
- Warmed 256-block process/setter probe: zero observed calls to replaceable C++
  new/new[]. It does not instrument aligned allocation, C malloc, locks or CPU.
- `PsyBassVoice` existing glide regression: **6/6 PASS** (before the final
  float-to-double gain-accumulator refinement; its glide code is unchanged).
- Existing built-in compliance: Psy Bass 184, Acid 191, Lead 240, Semantic FX 184,
  Atmos 240 checks PASS (before that final gain refinement). This is regression
  evidence, not a reason to retain the old Bass in the finished product.
- `git diff --check` PASS.

The ramp trajectory test first caught 3.63588e-6 accumulated error with a float
gain accumulator. The acceptance tolerance stayed 2e-6; the existing JUCE ramp
now uses a double accumulator and the test passes. Audio remains float.

JUCE dependency objects were not sanitizer instrumented. LeakSanitizer was
disabled because the sandbox does not support its process inspection. No leak,
TSan, full-CMake/Windows or DAW result is inferred from this local run.

Architecture Guard reviewed the actual DSP/state diff independently: no
BLOCKER/MAJOR in this isolated slice. The full host/state/UI integration will
require its own review.

## Remaining acceptance

**WAIT / draft.** Do not describe the Sound page or Full-on Bass as complete.
Replace the coupled POC Bass catalog entry and bindings, expose all direct
parameters to host automation with meaningful units, and persist their base
values independently of presets/modulation. Remove the superseded mapping,
rather than shipping old/new modes. Verify editor gestures, reverse automation,
state rejection/restore, generation and integrated rendering.

Windows Release/full CTest is required before merge. Listening, aliasing/synthesis
quality, factory presets and actual Cubase/REAPER/visual acceptance remain
integrated-milestone gates. No sound-quality or production-readiness claim follows
from parameter independence or test counts.
