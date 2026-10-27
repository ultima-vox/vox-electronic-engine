# Rack candidate evidence — 27 October 2026

Scope: unmerged PR #62, generic Rack first; Acid-specific DSP/UI and Bass DSP
are not changed. This report does not declare the production milestone complete.
Contracts: [parameters/state](../architecture/RACK_PARAMETER_CONTRACT.md),
[audio graph](../architecture/RACK_AUDIO_ROUTING.md).

## Local native build and tests

Linux native Release, C++20, pinned JUCE 9.0.1; real JUCE/DSP objects and the real
PluginProcessor/editor, not a browser UI or fake audio implementation. All **39
CTest targets build successfully**. Full final CTest: **38/39 PASS**, exit 8 due
to the unchanged Bass diagnostic below. No tests are skipped or thresholds raised.

| Check | Observed result |
| --- | --- |
| Audio graph | 33,139 checks, 18 scenarios PASS; stable SlotId recipients; 44.1/48/96 kHz × blocks64/128/256/512/1024 × mono/stereo |
| FX finite tails | 6,023,910 checks PASS; independent ASan/UBSan rerun PASS; maxfeedback, serial bounds, sample expiry/new input, retrigger, bypass and zero C++ allocation at expiry |
| Direct parameter contract | 1,385 checks PASS; independent ASan/UBSan run PASS; 128/512 bounds, metadata/hash identity, one-base controls, modulation isolation, native/named preset authority, rollback/unresolved state |
| Real JUCE APVTS bank | 25 checks PASS; typed endpoints/units, replay, other-slot isolation and literal/VST3-hash collision rejection |
| Actual processor state/scenes | 220 checks PASS; live bases → XML → restore, exact fixture audio rerender at nonunity master gain, complete scenes, factory null/throw/prepare failure rollback, malformed/duplicate/missing core PARAM rejection, exact incompatible named/payload preservation |
| Mutation gate | 10,009 checks PASS; nonblocking callback admission, nested controller ownership and exception recovery |
| Realtime MIDI boundary | 33,808 checks PASS, independently executed; C++ new plus GNU malloc/calloc/realloc wrapping; 64 KiB SysEx/dense input, held GUI keyboard lock, concurrent keyboard burst, fresh post-Panic NoteOff ownership |
| Rack shell integration | 10,276 checks PASS; actual master/routing/state/duck effects |
| Existing processor integration | 2,056,370 checks PASS |
| Reliability stress | 559,552 checks PASS, including schemas1..4 rejection preserving exact committed current5 state |
| Performance stress | 202,189 checks PASS; two instances, 16 slots each, 64 sends/four returns, 48 kHz/512 samples; aggregate realtime ratio 1.28865 in this shared container |
| Rack MIDI export | 320 independent checks PASS; actual SMF for slots01/02/16, matched NoteOffs, seed/channel/timing, selected helper, unique files, captured-byte isolation and failed-write rollback |
| Native editor | PASS; real widgets change APVTS, independent slot power, restored pattern length/revisions and instrument catalogs, supported window sizes and actual component captures |

The performance suite's existing ratio ceiling6 is a regression ceiling, not a
promised musician hardware/latency budget. A measured ratio above1 here does not
establish that this workload meets an audio deadline on target Windows hardware.
The C++ allocation tests do not imply a complete leak/OS/host allocation audit.
GNU heap wrapping is local Linux instrumentation, not Windows CRT instrumentation;
JUCE dependency objects were not all sanitizer-instrumented. LeakSanitizer was
unavailable in the earlier sanitizer runs. Headless native component snapshots
are not proof of a successful OS window/DAW peer or owner visual approval.

## Remaining Bass diagnostic

`bass_render_diagnostics`: 15/16 scenarios pass. `save_restore_rerender` fails its
existing max-difference threshold0.05 with max0.0656629, RMS difference2.33155e-6,
peak difference0. Same values appeared before the direct/state/FX/realtime fixes.
An earlier controlled baseline-processor relink also produced these values; that
comparison was not a full clean baseline-tree build. The precise cause remains
unproven. No Bass DSP or diagnostic threshold is changed to hide this result.
Keep #45/#58 follow-up explicit; the full local suite is not green.

The same diagnostic passes host MIDI lifecycle, generated profiles, block/rate
cases, transport/Panic, routing/audition and real MIDI export/re-import (12 NoteOns
and12 NoteOffs). These are infrastructure checks, not Full-on musical acceptance.

## Independent Architecture Guard

An independent reviewer inspected actual diffs and independently authored and ran
the realtime boundary tests. Final reviewed fixes: no audio-side JUCE keyboard
lock; bounded raw MIDI filtering before SysEx construction; deferred allNotesOff
preserves fresh key ownership; incompatible named values avoid current-descriptor
rounding; native editor follows restored revision/module identity. No remaining
blocker was found in those fixes. Required CI and manual gates remain open.

Intentional project/Rack/FX schema5/4/2 evolution is disclosed. Updating the obsolete
migration assertion to stronger explicit rejection of1..4, with exact state-byte
preservation, was reviewed as owner-authorized contract evolution. Regression
thresholds are not weakened and the old-format scenario remains tested.

## Windows result and stack repair

Windows run [40461084884](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/40461084884),
head3537554 (merge db9e7ad), completed Release build successfully, then failed
CTest:35/38 PASS; RackIntegration,EditorSmoke,Reliability exited SegFault.
**Bass rerender passed on Windows**. Visual capture/package stages were not reached;
this is a failed required check, not an accepted deliverable.

A local1MiB-stack run reproduced RackIntegration's crash. Compiled object sizes
on Linux explained the risk: Rack932,680 bytes and Processor1,030,408 bytes.
The fixed processing storage now lives in one construction-time heap allocation:
Rack9,936 bytes and Processor107,664 bytes. Router ownership/MIDI/control capacities
and persistent formats are unchanged; no linker/test stack increase was used.
Compile-time object-size gates protect Rack64KiB/Processor256KiB limits.

After repair, all38 local targets build and the full suite is again37/38 PASS
(the same Linux Bass diagnostic only). RackIntegration97,EditorSmoke,Reliability559,552,RT33,808 (GNU heap
wrapping),Performance202,189 checks pass under an actual1MiB Linux stack. Independent
reviewer recompiled the size probe and independently repeated the first three
low-stack targets successfully. Callback allocation probes remain zero. Fresh
Windows CI for the repaired exact head is still required; low-stack reproduction
is not a Windows crash dump or a substitute for that run.

Reproduction (native local build paths):

```bash
bash -c 'ulimit -s 1024; exec build/rack-production/rack_integration_tests'
bash -c 'ulimit -s 1024; exec build/rack-production/editor_smoke_tests'
bash -c 'ulimit -s 1024; exec build/rack-production/reliability_stress_tests'
```

## All-slot MIDI export completion

The previous helper accepted only slot01 and reused a fixed Bass filename.
The candidate now exports selected or explicit banks01..16 using unique temporary
paths; shared Settings provides a real Save MIDI action without Acid UI changes.
Independent320-check tests parse actual SMF, verify raw authored-input semantics,
seed determinism and channel/NoteOff/timing, and prove write failure preserves
existing content. Export/EditorSmoke/RealtimeBoundary pass under1MiB stack.
Native chooser interaction and host drag/drop remain unperformed manual gates.

## Delivery and milestone gates

- Earlier PR #62 head e6d892c380a540bb25ce7354f31f32585f1e86b4 had successful Windows
  run40329748342. That older result does not cover this consolidated candidate.
- Latest repaired Windows x64 Release/full CTest/package must pass before merge.
  Linux VST3 packaging is not a supported deliverable; its pre-existing non-PIC
  static dependency link problem was not changed. Local native test builds pass.
- Workflow captures actual shell, send/return utility and scrolled ducking controls
  in addition to the showcase. Owner visual approval remains REQUIRED AT MILESTONE.
- Cubase12–15 and current REAPER: scan/load, MIDI/automation, generated patterns,
  export/re-import, returns/ducking/tails, reopen/offline render, Panic and measured
  active workload remain REQUIRED AT THE INTEGRATED RACK MILESTONE.
- No standalone or external sidechain is claimed. No production Drums/Keys/Lead/FX
  or dedicated Acid UI is substituted by the deterministic test providers.

Disposition: **WAIT / do not call production accepted**. Preserve the known local
Bass failure and wait for the exact consolidated Windows checks and manual gates.
