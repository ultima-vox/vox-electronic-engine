# Implementation status

Updated: 27.10.2026.
Audited main: `ccc1b4dbe416d96370721674eb4857aec19f8fac`.
Product direction: Vox Trance Engine; installed binary: Vox Electronic Engine.

## Owner decision — separate instruments (27 October 2026)

Instrument DSP and instrument UI must be independently installed and updated,
not built into the Rack VST3. One VST3 remains the host. Current main and pending
PR #62 still use static built-in providers; a reviewed module loader, package,
and Acid external-module adapter do not yet exist. A fixed 16 × 64 slot automation
pool is selected; PR #65 implements an unmerged candidate, pending review and
real-host acceptance. Acid currently has 80 host-facing controls: the 64-endpoint
limit is an unresolved integration blocker.
This owner decision supersedes the previous static-only 1.0 decision. Details and
exit gates: [independent instruments](architecture/INDEPENDENT_INSTRUMENTS.md).
Rack / PR #62 belongs to the other agent; Acid / PR #59 is separately owned.

## Current priority and ownership

**Finish the generic Rack first; Bass follows.** Another agent owns Acid Synth
and its dedicated UI. The lead owns shared Rack/host/state contracts and Routing.
See [current task](tasks/RACK_PRODUCTION.md), [audit](testing/rack-capability-audit-20261026.md)
and [epic #53](https://github.com/ultima-vox/vox-electronic-engine/issues/53).

The Rack is a working MIDI/mix foundation, **not production-complete**. Confirmed
gaps in main: internal kick ducking bus, per-slot Send FX/return graph and their
Routing UI. Existing global post-rack effects do not implement those features.
Generic direct parameter/automation/state completion also remains open.

## Existing code, not a production-acceptance claim

- C++20, JUCE 9.0.1, CMake; Windows VST3 package workflow.
- Generic 16-slot Rack/Router and instrument/provider registry.
- Psy Bass, Acid, Lead, Semantic FX and Atmos modules.
- Step sequence, host PPQ sync, scheduling, MIDI export.
- Preset/state handling, modulation and post-rack effects.
- Shared VOX UI components and product editor.
- Automated regression, performance and reliability suites.

These facts describe source structure. Evidence and limitations are recorded
separately below; source presence is not production acceptance.

## Owner-reported blockers

| Symptom | Status | Required evidence |
| --- | --- | --- |
| Generation does not work | Original session cause not established; PR #51 scheduler/export fixes are integrated and the generation/publication/audio path exists | Reproduce any remaining failure with exact build/state, source mode, transport and event trace |
| Bass sounds like a continuous beep | Original session cause not established; direct main voice NoteOff/release/reset probe passes27 checks | Exact build/state and full event path; do not label ADSR broken from this symptom alone |
| Too much functionality without a reliable workflow | Reported | One complete Bass -> generation -> MIDI -> project restore path |

Do not label a guessed envelope or routing cause as confirmed. Lack of ducking or
sends does not by itself explain either symptom. Remaining Bass acceptance is
deferred after Rack, tracked in #45/#58.

## Production targets still incomplete

- Full-on Bass/Drums/Lead/Keys/FX as separate production rack instruments.
- Independent constrained Sound/Pattern generation for that complete lineup.
- Polyphonic, long-phrase musical model and versioned adapters.
- Complete Keys and Drum Kit workflows.
- Style-instrument packaging, commercial entitlements and offline licensing policy.
- Approved production UI and full Windows/Cubase/REAPER acceptance.

Lead DSP already has8 voices and Atmos6. The missing polyphonic/long-phrase
Sequence/Pattern/scheduler model is a separate gap. Dedicated AcidPanel is not
in audited main, but its development is separately owned. The active Rack editor
uses generic pages; BassPanel's presence does not mean it is the only active UI.
Full Bass acceptance requires [the entire selected VOX surface](product/FULLON_BASS_ACCEPTANCE.md),
not the reduced13/16-control experimental panel.

Existing generic Lead/FX code is not equivalent to an accepted Full-on instrument.

## Validation ledger

- Required Windows checks passed for integrated safety/scheduler PRs #46/#48/#50/#51.
- This audit performed source review and a direct main PsyBassVoice lifecycle
  probe:27 checks PASS with project ASan/UBSan. Dependency objects were not all
  instrumented; LeakSanitizer was disabled. The exact owner session was not reproduced.
- No new Windows build, DAW launch or listening acceptance was performed by this
  audit. Existing CI has Windows Release build/CTest/packaging jobs.
- CTest does not prove audible character, drag-and-drop in a DAW, or visual acceptance.
- CPU/RAM promises await target CPU, audio interface and buffer information.
- Existing test ceilings are regression guards, not end-user performance promises.

## Current work allocation

Rack state/lifecycle hardening and MIDI release recovery are integrated in main
(PRs #48 and #46; required Windows CI passed for both). See the
[lifecycle report](testing/rack-lifecycle-report.md) and
[router report](testing/rack-midi-hardening-report.md).
Live-control synchronization and onset-aware release/ratchet/export diagnostics
are also integrated (PRs #50/#51); see the
[verification report](testing/rack-live-controls-report.md).
Manual sound/DAW acceptance remains outstanding at integrated milestones,
not a per-PR handoff. Standalone is not required; VST3 remains the product.

- Documentation/archival: completed in PR #44.
- AGY's original [diagnostic packet](tasks/AGY_BASS_DIAGNOSTICS.md) is historical
  input to PR #51; remaining exact-session/musical acceptance is tracked in #45.
- Rack lead: #53–#57; Acid DSP/UI: external agent; Bass: #58 after Rack.
- PR #52 and local Bass/direct-control experiments are unfinished and unmerged;
  they are not the published shared interface or evidence of production Bass.
- UI PR #43: existing Sound workspace candidate. Re-evaluate against current workflows; do not merge or close automatically during cleanup.
- Pattern/API v2 and new style instruments: subsequent scoped implementation, not part of AGY's first task.
