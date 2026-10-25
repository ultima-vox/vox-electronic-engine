# AGY packet — Bass/generation diagnosis and reproducible audio renders

Priority: P0. Scope: existing Bass path only.
Result: reproducible evidence, meaningful regression coverage and minimal fixes for confirmed defects.
This is a development assignment, not a request to redesign the product.

## Start

Repository: https://github.com/ultima-vox/vox-electronic-engine
Task source: documentation PR #44.
Work in a separate checkout/worktree from the documentation worker.

If PR #44 is merged, branch from current origin/main.
If it is not merged, branch from current origin/main and read these instructions and current
product/contract docs from origin/docs/trance-engine-production-design using git show.
Do not merge the documentation branch just to obtain task text.

```powershell
git fetch origin
git switch -c agy/bass-generation-diagnostics origin/main
git show origin/docs/trance-engine-production-design:docs/tasks/AGY_BASS_DIAGNOSTICS.md
```

If that local branch already exists, inspect/reuse its work rather than overwriting it.
Record HEAD and whether the local working tree was clean.
Follow AGENTS, current product/contract docs and relevant tests.
Archived product specifications do not override the current scope.

You are not alone in the repository. Do not revert other workers.
One Lead integrates. Do not merge, force-push or close another worker's PR.

## Owner report

1. Generation appears not to work.
2. Bass sounds like a continuous beep.

These are reports, not confirmed root causes.
An old installed binary or selected state may differ from current main.
Record exact binary/source version and chosen preset/mode/channel/tempo/root.
If the user case cannot be reproduced, state that honestly and still deliver the diagnostic harness.

## File ownership

Allowed new work:
- `tests/diagnostics/BassRenderDiagnostics.cpp`;
- `tests/diagnostics/README.md`;
- focused Bass/generation regression tests under `tests/bass/` or `tests/integration/`;
- a single diagnostic target/test registration in root CMake as needed.

Inspect:
- `apps/vst3/PluginProcessor.h/.cpp`;
- `libs/modules/BuiltInProvider.cpp`;
- `libs/bass/`;
- `libs/generator/`, `libs/sequence/`, `libs/midi/`;
- `tests/integration/PartAudioIsolationTests.cpp`;
- `tests/instrument/InstrumentComplianceHarness.cpp`;
- existing state/routing/preset tests.

Minimal fixes may touch Bass, the built-in Bass provider and processor generation/publication
only after identifying a concrete defect and adding a regression.
Do not use this permission for refactoring unrelated modules.
If the fix needs a changed ABI/state schema/host IDs or shared timing/routing contract,
deliver the diagnosis and a proposed follow-up; do not silently change the contract.

Do not edit current docs, archive, shared UI, rack/router contracts, non-Bass providers,
build product identity, package name, pinned dependency version or existing test tolerances.
Do not replace the current engine with a new standalone synthesizer.
Do not implement Pattern v2, Drums or licence checks in this packet.

## Required tracing

Follow the real path:
UI/processor command -> selected slot/profile -> generation context -> provider ->
generated result -> state/publication -> scheduler -> instrument voice -> audio.

Inspect actual methods before using them. Existing processor entry points include
`loadSlotInstrument`, `selectSlot`, `assignSlotChannel`,
`selectedContent`, `applySelectedSoundPreset`, `generateSelectedPattern`,
`publishSequenceForAudio` and `createPartMidiFile`.
Do not invent Sound generation APIs: establish what exists versus what is still planned.

Record whether the generated event/state result actually changes for multiple seeds,
whether it reaches the scheduler, and whether scheduler events reach the selected Bass.
An unchanged result for one seed/profile does not establish failure.

## Render diagnostic executable

Add a reproducible CLI renderer using the real processor/provider pipeline.
A direct PsyBassVoice-only test may supplement it, not replace integration evidence.
Use a controlled playhead for host transport/PPQ. Do not require a GUI window or physical audio device.
Initialise JUCE appropriately for the linked processor tests.

Proposed target name: `bass_render_diagnostics`.
Document its actual CLI and output format; no command may be claimed until implemented.

Required scenarios:
- Host MIDI short note, sustained note then NoteOff, repeated equal notes.
- Internal generator at 145 BPM / 4/4 / fixed root and seed.
- Multiple seeds and at least two available Bass presets/profiles.
- Same state at 44.1 and 48 kHz, blocks 64 and 512.
- Start/stop and Panic; no hanging notes beyond the declared tail policy.
- Save -> restore into fresh instance -> rerender.
- Wrong-channel silence versus assigned-channel audio, and selected-slot audition if testable.
- Generated MIDI file and resulting re-import playback through Host mode.
- MIDI event counts/times alongside WAV, not just an RMS assertion.

Choose explicit note numbers and document octave naming; do not rely on DAW octave labels.
Use known preset IDs from the actual catalog.
For dry renders disable/bypass effects through supported state/API and report settings.
Do not call an already limited/processed output raw oscillator evidence.

Outputs under ignored `build/bass-diagnostics/`:
dry WAV, MIDI and a machine-readable manifest containing commit, patch/settings,
seed, tempo, root, sample rate, block size, scenario and measured values.
Write files outside the audio callback.
Include a text summary suitable for review.
Do not commit generated audio/build products into source.

## Evidence and regression rules

- Silence/non-finite samples/obvious stuck-note violations are objectively testable.
- Peak/RMS, event timings and envelope/tail measurements help explain sound.
- Non-zero RMS does not prove psy-bass quality.
- A passing test must fail against the confirmed pre-fix defect.
- No forced tolerance widening, disabled tests or hard-coded golden output hiding changed behaviour.
- No arbitrary claim that a musical bass must equal a particular Freesound spectrum.
- CPU measurements identify hardware/configuration; do not infer a supported instance count.

Use Freesound candidate links in PRODUCT_SPEC only for reference selection.
Do not embed third-party samples without checking each exact file's redistribution licence.
Do not claim to have listened if only metadata or waveforms were available.

## Verification

Use the existing Windows x64 Release configure/build/CTest/package workflow.
Run relevant Bass/integration/state/MIDI tests after changes and full CTest before handoff.
Check realtime allocations if the fix touches processBlock/voice processing.
Do not claim actual Cubase/REAPER acceptance from a mocked playhead.

For available DAWs record exact versions, settings and observed results.
For unavailable hosts provide the precise manual checklist and mark REQUIRED.

## Handoff

Return:
- branch, base/head SHAs and clean/dirty status;
- changed files and focused explanation;
- reproduced failures or explicitly unconfirmed cases;
- pre/post-fix event/state/audio evidence;
- actual commands and outcomes;
- WAV/MIDI/manifest artifact location;
- compatibility/realtime impact and remaining risks;
- AgentReport and merge recommendation.

Push the task branch and open a draft PR against main when the code/evidence is ready.
Do not merge it.
First completion goal is a reliable existing Bass path; new Full-on instrument identity/content
is a subsequent task after this baseline is understood.
