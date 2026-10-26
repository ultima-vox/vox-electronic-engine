# Bass scheduler and export integration

## Proven defects and candidate fixes

This integration adopts the six-file AGY PR #49 (head
`74836c6f55ce410e4c245d9596b5fe12f8738552`) on the current Rack safety work.
The AGY branch is unchanged. This is not proof of the exact owner-reported session.

- AGY correctly found an inactive legacy sequence in Bass MIDI export and a
  PPQ note-release countdown that ignored a new note's mid-block onset.
- Independent actual-source reproduction found AGY introduced a ratchet loss:
  48 kHz, 144 BPM, 512 samples, ratchet 2, gate .5 produced only ON 0/OFF 1250;
  required ON 2500/OFF 3750 never arrived. The old baseline emitted the second
  note too early (1988). A missing per-block carried-ratchet decrement is fixed.
- A new sweep checks absolute onsets/releases within one sample across rates
  44.1/48 kHz, BPM 144/145, blocks 64/512/1024, ratchets 1/2/3/8, gates 0/.5/1.
- Export reads the active slot sequence and channel; probability uses exactly
  the runtime generationSubSeed domain. Export does not modify legacy Parts.
- The diagnostic re-import now plays parsed SMF events through the real
  processor at explicit 145 BPM, checking non-default channel 3, balanced
  note releases, finite audible audio and saving the replay WAV.
- Output WAV is 32-bit float (peaks above 1 remain intact); dry effects use the
  actual clean preset. Generation/parameter/file/unknown-scenario failures
  cannot silently report success. Restore also checks the pattern fingerprint.

## Evidence and limits

The AGY original scheduler suite passed 35/35 despite the independently
reproduced ratchet loss. The integration sweep passes with real pinned JUCE
MIDI/core sources; final count and sanitizer results are recorded in the PR.
Diagnostic translation-unit syntax check against JUCE 9.0.1 passes locally.
Full processor diagnostic execution requires Windows CI and has not been run
locally. The original PR's 16/16 diagnostic claim is not reused as evidence for
this changed candidate. Windows full build/CTest/package is required before merge.

No external ABI, state schema, identities, UI or Rack routing model changes.
The scheduler remains audio-owned; no callback lock or allocation is added by
this patch. No test tolerance was widened. Explicit slot output transpose/filter
export semantics and full runtime-generated event manifests remain further work;
this candidate does not promise complete preview/export equivalence for all
routing configurations. Shared temporary export filename is also pre-existing.
Pattern identity is exact on restore; AGY's existing waveform bounds remain,
without claiming sample-identical oscillator phase. Factory timbre and DAW
acceptance are REQUIRED AT MILESTONE, not per technical PR.

## AgentReport

```yaml
agent_report:
  role: Lead / Integrator
  task: Integrate verified Bass scheduling/export repairs and honest diagnostics
  result: PARTIAL
  files_changed:
    - CMakeLists.txt
    - apps/vst3/PluginProcessor.cpp
    - libs/midi/GeneratedNoteScheduler.cpp
    - tests/midi/GeneratedNoteSchedulerTests.cpp
    - tests/diagnostics/BassRenderDiagnostics.cpp
    - tests/diagnostics/README.md
    - docs/testing/bass-diagnostics-integration-report.md
  contracts_touched: [MIDI event timing, deterministic probability export]
  assumptions: [manual host checks deferred to milestones]
  implementation: [onset-aware releases, cross-block ratchets, active sequence export, actual SMF replay]
  tests_added: [absolute ratchet timing sweep, processor diagnostic scenarios]
  verification:
    - command: /tmp/vox-scheduler-integration-tests-sanitized
      result: PASS
      notes: project and JUCE MIDI ASan/UBSan; core precompiled, LeakSanitizer disabled
    - command: g++ -fsyntax-only tests/diagnostics/BassRenderDiagnostics.cpp
      result: PASS
      notes: local JUCE headers and temporary generated-module include header
  architecture_changes: [none]
  migration_impact: [none]
  performance_impact: [bounded ratchet loop retained; not benchmarked]
  security_impact: [none]
  manual_acceptance_required: [Windows DAW and audible Full-on milestone]
  unresolved: [full processor CI, generated-event manifests, phase policy, routing export semantics]
  risks: [not a complete Full-on Bass production acceptance]
```
