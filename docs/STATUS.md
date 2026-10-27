# Implementation status

Updated: 27.10.2026.
Audited main: `ccc1b4dbe416d96370721674eb4857aec19f8fac`.
Product direction: Vox Trance Engine; installed binary: Vox Electronic Engine.

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

## Rack implementation candidate — 27 October 2026

PR #62 (`feat/rack-production`, unmerged) adds the accepted red header/16-slot
rail, master gain/meters, four wet FX returns with per-slot pre/post sends and
explicit kick-note event ducking. Settings opens shared audio routing. Generic
named instrument/APVTS banks, transactional host/complete-scene restore, bounded
FX tails and the lock-free keyboard/audio handoff are integrated in the candidate.
Selected/explicit slot MIDI export and a shared Settings save action are now present;
320 independent SMF/write-failure checks pass. The green Acid workspace is not redesigned; Acid/Bass DSP is unchanged.

Contracts: [audio graph](architecture/RACK_AUDIO_ROUTING.md),
[named parameters/state](architecture/RACK_PARAMETER_CONTRACT.md).
Project/Rack/FX schemas deliberately become 5/4/2; old POC project/complete-scene
formats are rejected under the owner-authorized replacement policy. Existing
plugin and parameter identities are not renamed.

Independent evidence includes FX-tail 6,023,910 ASan/UBSan checks, direct contract
1,385 checks, actual processor state/scene 268 checks, realtime MIDI/keyboard
33,808 checks with GNU malloc/calloc/realloc wrapping, and native editor restore/
selection/routing tests. Full final CTest/build results and limits are recorded in
[implementation evidence](testing/rack-production-evidence-20261027.md).

The earlier PR #62 head e6d892c had successful Windows run40329748342; this does
not verify subsequent direct/state/FX/realtime changes. Run40461084884 for consolidated3537554 built successfully but failed three
Windows tests; Bass rerender passed there. The candidate repairs the reproduced
1MiB-stack overflow risk by moving fixed Rack processing storage to construction-time
heap allocation; all three formerly crashing cases pass independent low-stack
repeats. Windows run40469745576 for head760e751 subsequently passed Release build/full
CTest/native capture/package/upload. A further regression repair preserves future
incompatible named parameter domains while keeping dormant live endpoints safe;
268 processor checks pass locally. Fresh exact-head Windows checks for that repair
are required before merge.
Owner visual acceptance and integrated Cubase/REAPER acceptance remain separate gates. Do not close #53 or describe the Rack as production accepted.
