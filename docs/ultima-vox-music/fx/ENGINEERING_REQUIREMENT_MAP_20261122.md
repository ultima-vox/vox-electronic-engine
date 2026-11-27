# VOX FX exact requirement-to-evidence map — 2026-11-22

**Product-completion tranche / Draft / HOLD / NO MERGE / NO RELEASE.** This
map is the current-source reconciliation record requested by owner/TL comment
[#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7210838569).
It supersedes stale prose that calls an older PR snapshot the integrated
candidate; older evidence remains historical and is not silently reattributed.

## Source and evidence boundary

| Item | Exact value / status |
|---|---|
| Assigned Arena branch | `arena/3f9aa012-vox-electronic-engine` |
| Assigned branch base | `c7ee608a0879087d9dade1f2471c69a1bde92732` (`next/fx-development`) |
| Arena documentation head before this tranche | `ef0f975` |
| Current product-completion candidate | `a6365ebf88414c4ff5813df396e5b12f2fc8e49b` (pushed after this record) |
| Exact remote PR #156 head | `5c2f122c0203dde57bda7cd563340164601579b4` |
| Accepted #170 tooling/parser source | `cf67051aadec802223c49819c72ed264c1eac7cc`; parser implementation `b79d517b5e6a5bc11168b3331956876126c3e42a`; parser `1.11.0` |
| Local lineage result | Grafted checkout produced no `git merge-base` SHA; `git rev-list --left-right --count HEAD...refs/remotes/origin/pr/156` returned `11 233`. The PR head is fetched as `refs/remotes/origin/pr/156` and is **not merged**. |
| Review status | #156 and #170 remain Draft / HOLD / NO MERGE / NO RELEASE. No Windows dispatch, full matrix, merge, release, or production receipt exists. |
| Content policy | Frozen `232` review-only cues, `0` approved; `600` approved factory programs unchanged. No filler, catalog expansion, audio transfer, or approval is inferred. |

**Evidence vocabulary:** `Implemented-local` means the binding exists in the
current Arena source and the named standalone/local fixture ran. `Partial`
means code or a subset of the behavior exists but a required binding, breadth,
or qualification gate is open. `Missing` means no qualifying current evidence
or implementation for the requested outcome. `Host-none` means no actual
Cubase/REAPER/Windows host evidence is being claimed; Linux standalones,
synthetic PlayHead tests, and offscreen JUCE paths do not substitute for it.

## FX-01…FX-07 source/test/host map

| Requirement | Current source binding | Local executable evidence | Actual host / Windows evidence | State and next gate |
|---|---|---|---|---|
| **FX-01 shell, IDs, first sound** | `products/fx/Processor.cpp`, `Processor.h`; `products/fx/parameters.csv`; `SuiteIdentity.cmake`; `processBlock` and output/duration attachments | `tests/suite/SuiteShellTests.cpp`, `FxProcessorTests.cpp`, `FxNoiseRiserTests.cpp` | **Host-none.** No exact-head Windows bundle, VST3 validator, scan, or automation round | **Partial/Implemented-local.** Build exact integrated source; validate VST3 and scan/automation in Windows |
| **FX-02 four layers/timeline** | `engine/NoiseRiser.h` event snapshots; `LayerPanel.cpp`; `CurveEditor.cpp`; `engine/Timeline.h`, `CurveTarget.h`; four layer cards and solo/drag revision guard | `FxLayerTests.cpp`, `FxCurveTrajectoryTests.cpp`, `FxTimelineTests.cpp`, `FxProcessorTests.cpp` | **Host-none.** No real DPI/focus/readback/drag/reopen/bounce | **Partial.** Native wrapper and project-reopen evidence remain |
| **FX-03 twelve source families, controls, modes/key** | `engine/NoiseRiser.h`; `SourceSound.h`; `ProgramCodec.h`; `FxEditor.cpp` `SourcePanel`; new signed `FamilyControls` and FPRG10 migration; filters, transpose, Glitch, Granular Travel | `FxSourceSoundTests.cpp`, twelve family tests (`FxDownlifterTests` through `FxGranularTests`), `FxProgramCodecTests.cpp`, `FxPlaybackModeTests.cpp`, `FxBarEventTests.cpp` | **Host-none.** No Windows/DAW source audibility, host tempo/hold/seam, or native restore | **Partial.** Local tranche closes type-specific source control binding; listening and host behavior remain |
| **FX-04 Motion/four serial FX/quality/tails** | `engine/Effects.h` (four slots, type-specific amount semantics, ADAA Drive/Wavefold); `MacroRouting.h`; `PostMotion.h`; `Processor.cpp` post path; `FxEditor.cpp` dynamic effect semantics | `FxEffectsTests.cpp`, `FxNonlinearAliasTests.cpp`, `FxMacroRoutingTests.cpp`, `FxEffectTailTests.cpp`, `FxRuntimeStressTests.cpp`, `FxProcessorTests.cpp` | **Host-none.** No loaded Windows deadline proof, host automation/bounce, or perceptual approval | **Partial.** ADAA and semantic controls are local; FX-T18, loaded RT and listening stay open |
| **FX-05 authoring/samples/export/library** | `SoundGenerator.h`, `Generator.h`, `GeneratorState.h`; `ProgramStore.h`; `Preview.h`; `PresetLibrary.h`; `SampleController.h`, `SampleStore.h`; `FileExport.h`; `FxEditor.cpp` Generator/Preview/Browser/Save/drag | `FxGeneratorTests.cpp`, `FxGeneratorStateTests.cpp`, `FxSoundGraphTests.cpp`, `FxSampleGranularTests.cpp`, `FxSampleControllerTests.cpp`, `FxFileExportTests.cpp`, `FxOfflineRenderTests.cpp`, `FxPortablePresetTests.cpp`, `FxPresetLibraryTests.cpp` | **Host-none.** No Windows chooser/OLE, project-copy/relink, or DAW reopen/unload | **Partial.** Existing implementation is present and preserved; actual native transfer and user listening remain |
| **FX-06 native workspace/diagnostics** | `FxEditor.h/.cpp`, `LayerPanel`, `CurveEditor`, `SourcePanel`, `MotionPanel`, `PresetBrowser`; processor telemetry, playhead, clip/tail status | `FxProcessorTests.cpp`, `FxCurveTrajectoryTests.cpp`, `FxProgramStoreTests.cpp`, motion/pattern/export local fixtures | **Host-none.** No native display, 100–200% DPI, screen reader, focus/shortcut, or docking pass | **Partial.** GUI qualification is still a Windows/native gate |
| **FX-07 shipping** | `.github/workflows/suite.yml`; `WINDOWS_ENGINEERING_GATE_20261119.md`; installer/validator/checklist documents; `RELEASE_LEDGER.md` | No current Windows runtime/build/validator artifact | **Missing.** No exact-head x64 Release artifact, Steinberg validator, installer, Cubase/REAPER result, rights/listening sign-off | **Missing.** Separate owner-authorized Windows run only after exact integrated candidate |

## FX-T01…FX-T20 exact map

| Test requirement | Source/test evidence in current tree | State | Actual host evidence / open gate |
|---|---|---|---|
| **FX-T01 family reference renders** | `NoiseRiser.h`, `SourceSound.h`; `FxNoiseRiserTests.cpp` plus twelve family tests; `FxSourceSoundTests.cpp`; family controls persist in `ProgramCodec.h` | Partial | Synthetic/local waveform checks only; owner listening and rights not done |
| **FX-T02 layer independence/snapshots** | `NoiseRiser::noteOn`; `ProgramStore::editLayerIfRevision`; `LayerPanel.cpp`; `FxLayerTests.cpp`, `FxProgramStoreTests.cpp`, `FxProcessorTests.cpp` | Implemented-local / host partial | Real host restore, four-layer automation/readback and drag under restore open |
| **FX-T03 twenty trajectories** | `CurveTarget.h`, `CurveEdit.h`, `CurveEditor.cpp`, `ProgramCodec.h`; `FxCurveTrajectoryTests.cpp`, `FxTimelineTests.cpp`, `FxProgramCodecTests.cpp` | Implemented-local / host partial | Structural saved curves are not new host lanes; keyboard/focus and actual project reopen open |
| **FX-T04 six modes/note lifecycle** | `NoiseRiser.h` mode/NoteOn/NoteOff/bar paths; `FxPlaybackModeTests.cpp`, `FxBarEventTests.cpp`, `FxProcessorTests.cpp` | Partial | Synthetic variable blocks and Linux paths only; real host block/seek/loop/tempo endpoint open |
| **FX-T05 FIFO/stop/capacity** | `NoiseRiser.h` FIFO MIDI ownership, retirement, grain mask, Panic; `FxNoiseRiserTests.cpp`, `FxGranularTests.cpp`, `FxProcessorTests.cpp` | Implemented-local / host partial | Host MIDI ordering and saturation listening open |
| **FX-T06 bar grid/PPQ/locate** | `Timeline.h`, `Transport.h`, `Processor.cpp`; `FxBarEventTests.cpp`, `FxTransportTests.cpp`, `FxProcessorTests.cpp` | Implemented-local / host partial | Real tempo ramps, seek/loop/discontinuity, exact Transition bar endpoints open |
| **FX-T07 tuning/keyless** | `SourceSound.h::Tonality`, `NoiseRiser.h`, `SourcePanel`; `FxSourceSoundTests.cpp`, `FxProcessorTests.cpp` | Implemented-local / listening partial | Mixed musical material and keyless behavior need owner listening/DAW round |
| **FX-T08 eight macros/64 typed routes** | `MacroRouting.h`, `PostMotion.h`, `Processor.cpp`, `MotionPanel`; `FxMacroRoutingTests.cpp`, `FxSoundGraphTests.cpp`, `FxRuntimeStressTests.cpp` | Implemented-local / host partial | Broader permutations, macro readback and actual host automation open |
| **FX-T09 serial FX/quality/tails** | `Effects.h`, `FxEditor.cpp`, `EffectTail` path; `FxEffectsTests.cpp`, `FxNonlinearAliasTests.cpp`, `FxEffectTailTests.cpp`, `FxOfflineRenderTests.cpp` | Partial | ADAA local result; loaded long-tail automation, Windows deadlines and listening open |
| **FX-T10 granular 64/WAV/leases** | `SampleReference.h`, `SampleDecode.h`, `SampleStore.h`, `SampleController.h`, `NoiseRiser.h`, `Preview.h`; `FxSample*Tests.cpp`, `FxProcessorTests.cpp` | Partial | External FS blocked-I/O/unload and exact-path project restore remain host gates |
| **FX-T11 Generate/Mutate/locks/A-B/User** | `Generator.h`, `GeneratorState.h`, `ComparisonState.h`, `PresetLibrary.h`, `Processor.cpp`, `FxEditor.cpp`; `FxGeneratorTests.cpp`, `FxGeneratorStateTests.cpp`, `FxSoundGraphTests.cpp`, `FxProgramStoreTests.cpp` | Partial | Editable Program and session A/B are local; persistent curation/factory approval and DAW sample portability open |
| **FX-T12 120→300→600** | `content/FactoryCandidates.h`, `ReviewWave*`, `evidence/fx-review-232-working-matrix.tsv`, `FxFactoryCandidatesTests.cpp`, `index-fx-private-review.py` | Missing as approval | Exactly 232 review-only/0 approved is frozen; no approval or expansion until owner listening |
| **FX-T13 Trigger Pattern/editable SMF** | `Pattern.h`, `PatternEdit.h`, `PatternCodec.h`, `FxEditor.cpp`, `FileExport.h`; `FxPattern*Tests.cpp`, `FxFileExportTests.cpp`, `FxProcessorTests.cpp` | Implemented-local / host partial | Real OLE/SMF transfer, seek/tempo and host block timing open |
| **FX-T14 WAV/MIDI create-only lifecycle** | `OfflineRender.h`, `FileExport.h`, `FileCommit.cpp`, `FxEditor.cpp`, `Processor.cpp`; `FxOfflineRenderTests.cpp`, `FxFileExportTests.cpp`, lifecycle evidence docs | Partial | No-replace/create-only implementation exists; Win32 chooser, drag consumer and DAW transfer open |
| **FX-T15 editor/accessibility** | `FxEditor.h/.cpp`, `SourcePanel`, `CurveEditor`, `MotionPanel`, `PresetBrowser`; automation/static/native evidence files | Partial | No actual Windows keyboard/focus/screen-reader/DPI result |
| **FX-T16 state/reopen** | `ProgramCodec.h`, `ProgramStore.h`, `PresetLibrary.h`, `SampleController.h`, `Processor.cpp` state read/write; `FxProgramCodecTests.cpp`, `FxProgramStoreTests.cpp`, `FxPortablePresetTests.cpp`, `FxSampleControllerTests.cpp` | Implemented-local / host partial | Legacy migration and malformed state are local; wrapper/project reopen and missing-path relink in DAW open |
| **FX-T17 exact-head build/validate/artifacts** | `.github/workflows/suite.yml`, `WINDOWS_ENGINEERING_GATE_20261119.md`, `evidence/run-fx-windows-t18.ps1`, `VerifyPinnedSourceCase.cmake` | Missing | Static plan only; no exact integrated Windows run, validator, install artifact, or SHA |
| **FX-T18 no allocation/blocking/deadlines** | `processBlock`, bounded grain/effects state; `FxRuntimeStressTests.cpp`, `audit-fx-timing.py`, accepted parser/runbook/qualification plan | Open / diagnostic-only | Accepted replay is `OFFLINE_REPLAY_PASS`, not production qualification; `5/4/9` observed vs `5/3/8` reconciled; CPU remains UNKNOWN/insufficient; no full matrix |
| **FX-T19 source/rights** | recipe/provenance docs, `SampleReference.h`, `PortablePreset.h`, frozen transfer/index files | Partial | Hash/index checks are not rights or distribution approval; no owner rights sign-off |
| **FX-T20 owner round** | `WINDOWS_ENGINEERING_GATE_20261119.md`, `RELEASE_LEDGER.md`, DAW checklist to be prepared after build | Missing / not ready | One separately authorized Cubase/REAPER round after technical gate and owner listening |

## Local outcomes at candidate `a6365ebf88414c4ff5813df396e5b12f2fc8e49b`

The checkout has no `cmake` and no JUCE dependency tree, so CMake/CTest,
JUCE processor/editor, VST3, Windows, and DAW tests were not executable here.
Targeted JUCE-independent C++ fixtures were compiled with `/usr/bin/g++`
`-std=c++20 -O2 -Wall -Wextra -pedantic` (and `-pthread` for the store) and
ran to zero failures:

- `FxProgramCodecTests`, `FxProgramStoreTests`, `FxSourceSoundTests`,
  `FxGeneratorTests`, `FxGeneratorStateTests`;
- `FxEffectsTests`, `FxNonlinearAliasTests`, `FxEffectTailTests`, and
  `FxMacroRoutingTests`; `FxRuntimeStressTests` was **not run** because it
  includes the JUCE-backed `Processor.h` path;
- `FxNoiseRiserTests`, `FxFormantTests`, `FxSquelchTests`, `FxGlitchTests`,
  `FxGranularTests`, `FxFamilyDifferentiationTests`;
- `FxPlaybackModeTests`, `FxBarEventTests`, `FxCurveTrajectoryTests`,
  `FxPatternCodecTests`, `FxPatternPlaybackTests`, `FxPatternGridTests`,
  `FxOfflineRenderTests`, and `FxFactoryCandidatesTests`.

The standalone executables emitted the existing non-fatal compiler warning in
`MacroRouting.h` about a one-line `for` statement. No source warning was
reclassified as a product failure. The accepted parser fixture/negative checks
and Python compilation remain prior evidence; no live ETW or Windows run was
performed in this tranche.

## Prioritized P0/P1/P2 ledger

### P0 — blocks any qualification/release claim

1. Reconcile the exact integrated source lineage: owner must identify whether the
   candidate is the assigned Arena branch, PR #156 head, or an authorized
   integration including #170 tooling. Do not merge/rebase by assumption.
2. Produce one exact candidate SHA, tree/dependency/compiler identity, Windows
   x64 Release build, validator/install artifacts, and SHA manifest. The
   Windows run remains unrequested and unauthorized here.
3. Resolve the production tracerpt/logman contract, four-runner-service
   baseline, CPU method or explicit wall-only policy, and actual measured
   workload binding. Parser `1.11.0` is frozen and diagnostic-only.
4. Keep FX-T18 fail-closed: no production receipt, no timing qualification, no
   full matrix, no merge, and no release.

### P1 — product/host readiness before owner round

1. Run the local source/codec/effects regressions after this tranche; obtain an
   exact candidate SHA. Run Windows only after separate authorization.
2. Validate the existing Generator → editable Program → Preview → Review/User
   browser → project state/reopen flow in the actual native wrapper, including
   sample lease/relink/hash behavior and create-only WAV/SMF Save/drag.
3. Exercise all six modes, 4/8/16-bar Transition, 20 trajectories, Motion,
   Pattern, source-family controls, dynamic type-specific FX semantics, focus,
   keyboard and 100–200% DPI in the native Windows wrapper.
4. Prepare the Cubase/REAPER acceptance checklist and one listening handoff
   from the frozen 232-cue inventory. Owner listening must identify gaps before
   private delivery or any catalog expansion.

### P2 — content and release completion after P0/P1

1. Owner-directed listening of the exact render/recipe handoff; record role,
   style, mode, render/recipe hashes and authorized private destination before
   copying any raw audio.
2. Rights/distribution sign-off and approved factory waves `120 → 300 → 600`.
   Current state remains `232 review-only / 0 approved`; technical metrics never
   become approvals.
3. Final owner DAW round, installer/package acceptance, and release decision.

## Listening handoff contract

Use exactly one frozen inventory:
`evidence/fx-review-232-working-matrix.tsv` SHA-256
`72fd9042415c70fec14e760ce16ebe4ae852bb50118c2c9e6e6deb6f039a1213`.
The existing transfer index and member checksums are inventories only. A future
owner-authorized handoff must attach the exact candidate SHA, render recipe
hashes, source/portable preset hashes, role/style/mode coverage, and the
permitted recipients/destination. No private audio delivery, new cues, or
approval occurs before the owner identifies an audible gap and authorizes it.
