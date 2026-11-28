# VOX FX product-completion checkpoint — 2026-11-27

**Development candidate / Draft / HOLD / NO MERGE / NO RELEASE / NO DISPATCH.**
Code candidate: `1625719fd5755fa1516d058b0b272a963aec33c9` on the assigned
Arena branch (parent `2e6b0c70a29d1f0486fcc23d0410c48d0882e94f`). This is the
formal answer required by [PR #156 review comment
#7211163434](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7211163434)
(“Fix the P1 items first” and the follow-up #21 checkpoint request). It is not
Windows, Steinberg-validator, DAW, loaded-RT, listening, production-receipt,
RC, merge or release evidence.

## 1. Invariants held at this checkpoint

Verified by `git diff --stat 2e6b0c70..1625719` — five files only
(`Effects.h`, `FxNonlinearAliasTests.cpp`, `FxProgramCodecTests.cpp`,
`FxPortablePresetTests.cpp`, `FxGeneratorTests.cpp`) plus this documentation
tranche. Specifically unchanged:

- **Host parameter IDs**: `products/fx/parameters.csv` untouched — all 19
  shipping IDs and their order.
- **Wavefold type ID**: `Type::wavefold` and its position in `Type` untouched.
- **Frozen diagnostic parser `1.11.0`**: no parser source change; counters
  remain as recorded.
- **Create-only WAV/MIDI export policy** (owner, §20.3 P0): untouched.
- **Frozen review inventory**: `products/fx/content/` untouched — exactly
  `232 review-only / 0 approved`; `600` approved-program target unchanged.
- **Portable preset / FPRG v10**: no schema change in this tranche — v10 was
  added by the prior work and is now covered by explicit tests (below).
  Forward-only contract and “do not silently down-convert” remain.

## 2. TL P1 remediation status (comment #7211163434)

| Review item | Status at `1625719` | Evidence |
|---|---|---|
| Restore original memoryless Wavefold; re-measure on identical fixtures | **DONE** | [WAVEFOLD_RESTORE_20261127](WAVEFOLD_RESTORE_20261127.md) §1: `wavefoldTransfer` is bit-identical to `5c2f122` over 4 M samples incl. clamp binding |
| Keep ADAA Wavefold experimental pending explicit approval | **DONE** | experimental API retained, never called by `Chain`; fixture measures it as a candidate only |
| Strengthen nonlinear regression (explicit success, real reference incl. clamp, actual phase/latency, passband, transients, boundaries) | **DONE** | WAVEFOLD doc §3; `FxNonlinearAliasTests` 8 sections, `0 failure(s)` + ASan/UBSan clean |
| Drive: measure compatibility against legacy projects (no regression claim from alias metrics) | **PARTIAL — automated part done** | RMS/DC/peak vs legacy transfer ≤3.5 % observed; **listening decision remains an owner gate** |
| FPRG v10 forward compatibility review | **DONE** | v10 block + migration tests in `FxProgramCodecTests` / `FxPortablePresetTests` / `FxGeneratorTests` (all `0 failure(s)`); no codec change required |
| Frozen 232 / 0 inventory | **HELD** | §1 above |
| Exact-SHA test evidence for every claim | **DONE** | §5 tables below |

## 3. Outstanding product requirements — local implementation status (task 5)

This is an audit of **local implementation** against the Unified Production
Specification and the requirement map — it is **not a claim that all product
requirements are complete**: Windows/DAW/listening/owner gates below remain
open, and no requirement is promoted to qualified by this audit. Code-level
evidence only, never host evidence:

| Requirement | Local implementation status | Evidence / gap |
|---|---|---|
| Modulation: every browser destination wired to a real parameter | **COMPLETE** | `MotionPanel.cpp` exposes all 17 destinations; each routes through `MotionStore::route` → `Processor` application path |
| Persistent A/B (not session-only) | **COMPLETE** | `FxEditor` A/B section + `FxEditor::comparison_*` members persisted in `Processor` state — STATUS.md’s “session-only” note was stale |
| Browser: source-type facets + character/family comparison + audition | **COMPLETE** | `FxBrowser` facets (type, family, character, bars, key/scale, pattern…) |
| Export: prepared WAV/MIDI Save + drag; create-only policy | **COMPLETE (implementation)** | `FileExport.h`, `FileExportActions`, `FxFileExportTests` create-only/no-clobber coverage; Windows drag proof remains a Windows gate |
| FPRG v10 compatibility and migration | **COMPLETE** | §2 row; forward-only reader/writer tests incl. reject-unknown-version |
| Generator duration lock persistence | **NOT A PRODUCT GAP** | No UI exposes duration lock; FGEN v4 `encode` *deliberately rejects* `locks.duration` and `FxGeneratorStateTests` asserts the rejection. Persistence would require an approved FGEN v5 if a UI is ever requested — no silent schema change |
| Wavefold shipping restore / experimental ADAA | **COMPLETE** | §2 |
| Host IDs / parameters.csv / parser 1.11.0 / create-only export | **HELD** | §1 |
| Everything Windows/DAW/listening/CI-gated | **OPEN — by authorization** | §4 |

## 4. Implementation slices (TL §21.1) and blockers

**Slices delivered in this checkpoint:**

1. **Slice A — FX-04 shipping restore (complete, `1625719`)**: memoryless
   Wavefold restored as shipping implementation; ADAA isolated under
   `experimental::`; `foldPrevious` state removed from `Chain`.
2. **Slice B — nonlinear regression hardening (complete, `1625719`)**:
   `FxNonlinearAliasTests` rewritten to the review’s fixture list; Drive
   legacy-compatibility automated metrics; ASan/UBSan clean.
3. **Slice C — FPRG v10 compatibility evidence (complete, `1625719`)**:
   forward-compatibility and migration tests across codec, portable and
   generator suites; frozen review inventory re-verified untouched.
4. **Slice D — product-completion audit closure (complete, this docs
   tranche)**: requirements above marked with code evidence; stale STATUS
   claims corrected in this checkpoint.
5. **Slice E — Windows x64 engineering candidate preparation (complete,
   preparation only)**: §6 below. **Not dispatched.**

**Proposed next slices (not started, need owner/TL direction):**

6. FGEN v5 duration-lock persistence — only if a duration-lock UI is
   requested (schema change requires review).
7. Windows qualification slice: exact-HEAD Release suite + Steinberg
   validator + SEGFAULT re-run — only with CI dispatch authorization.

**Initial blockers (§21.3), unchanged in kind:**

1. Four prior Windows processor SEGFAULTs at historical exact heads; queue
   contention delayed their re-run — both need CI capacity and dispatch
   authorization (this branch does not dispatch CI).
2. §20.2 decisions: `232 review-only` is frozen; owner has not yet selected
   waves B or C toward `600`, and listening has not begun.
3. Drive ADAA listening decision (owner gate).
4. Windows DAW/create-only export acceptance (owner/Windows gate).
5. No Steinberg validator execution at any SHA in this checkpoint.

## 5. Executed test evidence (executable, local Linux; SHA-attributed)

Tooling: `g++ 12.2.0`, `-std=c++20 -O2 -Wall -Wextra -pedantic`; JUCE-based
suites via CMake/Ninja against JUCE `9.0.1` at pinned commit
`e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` (strict suite pin verified by
`vox_verify_pinned_source`). Per TL comment
[#7212562366](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7212562366)
each result is attributed to the SHA it was executed at, and the raw logs
are retained:

- **Standalone harness runs — executed at code SHA `1625719`** (recipes in
  WAVEFOLD doc §3): the FX suites in the table below.
- **Full `ctest -L suite` — executed at docs HEAD `8ebdec0`** (clean tree;
  its code tree is byte-identical to `1625719`, the delta is documentation
  only): `100% tests passed, 0 failed, 0 skipped out of 78`, 130.62 s.
  Retained artifacts:
  - raw console log: `evidence/ctest-suite-8ebdec0-20261127.txt`
  - JUnit XML: `evidence/ctest-suite-8ebdec0-20261127.xml`
  - build/dependency/tree provenance:
    `evidence/ctest-suite-8ebdec0-20261127.provenance.txt`
  - CTest `LastTest.log` remains in `build-linux/Testing/Temporary/`.

| Suite | Result | Executed at |
|---|---|---|
| FxNonlinearAliasTests (+ ASan/UBSan) | `0 failure(s)` | `1625719` |
| FxEffectsTests / FxSoundGraphTests / FxEffectTailTests / FxMacroRoutingTests | `0 failure(s)` each | `1625719` |
| FxProgramCodecTests (v10 migration) | `0 failure(s)` | `1625719` |
| FxPortablePresetTests / FxGeneratorTests / FxGeneratorStateTests | `0 failure(s)` each | `1625719` |
| FxSourceSoundTests / FxOfflineRenderTests / FxProgramStoreTests / FxPlaybackModeTests / FxBarEventTests | `0 failure(s)` each | `1625719` |
| Pattern ×4 / Factory candidates / Sample store·reference·granular / Transport / CurveTrajectory / FamilyDifferentiation | `0 failure(s)` each (11 suites) | `1625719` |
| All 78 `suite`-labelled tests incl. JUCE-dependent FileExport, SampleDecode, VST3 host, identity/provenance | `78/78 passed`, `--no-tests=error` | `8ebdec0` |

These are developer-reported local Linux results (TL: not independently
executed). Frozen-item spot checks at `1625719`: `parameters.csv`
byte-identical to parent; `products/fx/content/` byte-identical to parent;
no file outside `products/fx/engine`, `tests/suite`,
`docs/ultima-vox-music/fx` changed.

## 6. Integrated Windows x64 engineering candidate — preparation receipt (task 6)

Per [INTEGRATED_CANDIDATE_PLAN_20261119](INTEGRATED_CANDIDATE_PLAN_20261119.md)
and TL comment
[#7212562366](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7212562366):

- **Exact source**: `8ebdec0a9a3645d9cdf7907fe0b6216eb5b62f85`, independently
  verified by the TL as two commits ahead of dedicated product base
  `2e6b0c70`, 11 changed files; PR #170 remains `2e6b0c7`. **Lineage: the
  product branch already descends from #156 — no cherry-pick or rebase is
  needed merely to reconstruct lineage**, and no history rewrite occurs.
- **Preflight executed (parse-only, no PowerShell execution, no CI)**:
  `python3 .github/scripts/prepare-fx-windows-preflight.py` at `8ebdec0`
  → `Checkout 8ebdec0…; 16 PowerShell run blocks extracted, not executed;
  SuiteIdentity.cmake matches workflow bundle names: VOX Trance Synthesis,
  VOX Drums, VOX FX`. The Windows-side `Test-FxWindowsPreflight.ps1` still
  requires **actual Windows PowerShell 5.1 on the intended runner** (not
  run).
- **Run proposal**: one exact-source Windows x64 engineering run is proposed
  in
  [WINDOWS_RUN_PROPOSAL_20261127.md](WINDOWS_RUN_PROPOSAL_20261127.md) —
  HAMUNAPTRA self-hosted only (no hosted fallback), clean Release,
  full-suite CTest, Steinberg validator on all three bundles, SHA
  manifests, CDB/failure diagnostics, installable artifacts if available.
- **Build definition (from `.github/workflows/suite.yml`, not run here)**:
  `cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON
  -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON
  -DFETCHCONTENT_SOURCE_DIR_JUCE=<pinned JUCE> -DVOX_FX_DIAGNOSTIC_SYMBOLS=ON`
  (Visual Studio 17 2022 generator), `cmake --build … --config Release
  --clean-first --parallel`, then `ctest --test-dir build-suite -C Release
  -L suite --no-tests=error --timeout 180 --output-on-failure
  --output-junit …`, then the Steinberg validator over all three
  `*.vst3` bundles.
- **Provenance required in the receipt**: exact source SHA, JUCE
  `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, VST3 SDK
  `v3.8.0_build_66` / `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`,
  MSVC/CMake/CTest versions, bundle inventory SHA-256s, validator log,
  CTest log/JUnit.
- **Status: PREPARED, NOT DISPATCHED.** No Windows CI is triggered from
  this branch; dispatch requires explicit owner authorization, and no PR
  creation or ref move happens without CI routing coordination. Existing
  #156/#170 HOLD/NO MERGE stands.

## 7. Release evidence ledger (§21.4)

The exact-HEAD matrix remains the
[ENGINEERING_REQUIREMENT_MAP_20261122](ENGINEERING_REQUIREMENT_MAP_20261122.md)
table (FX-T01…FX-T20), with Unified TZ numbering per TL comment #7212562366.
At this checkpoint:

- **FX-T09** (nonlinear quality): local Linux evidence now complete per
  WAVEFOLD doc; Windows/audio/manual columns remain `open`.
- **FX-T16** (codec/state, FPRG v10): local migration evidence complete;
  Windows column open.
- **FX-T17** (Windows validator): unchanged — no validator run at any SHA
  in this checkpoint; covered by the proposed exact-source run.
- **FX-T18**: unchanged — prior deadline misses stand; no new measurements;
  independent of the proposed engineering run.
- **FX-T20** (owner round): unchanged — no owner acceptance round held.
- Every other row: unchanged from the requirement map; no row is promoted to
  qualified by this checkpoint.

`RELEASE_LEDGER.md` carries the dated entry for this tranche.

## 8. What this checkpoint does *not* claim

No Windows execution, no CI dispatch, no PR #170 change, no merge into
`main`, no release, no listening approval, no production receipt, no
catalog/program approval (232 frozen), no export-policy change, no parser
change, no host parameter ID change, no silent schema change.
