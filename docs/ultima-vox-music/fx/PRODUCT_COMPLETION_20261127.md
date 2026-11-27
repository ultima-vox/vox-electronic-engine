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
(`Effects.h`, `Generator.h`, `FxProgramCodecTests.cpp`,
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

## 3. Outstanding product requirements — completion status (task 5)

Local implementation audit against the Unified Production Specification and the
requirement map, with code-level evidence (this is implementation evidence
only, never host evidence):

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

## 5. Executed test evidence at `1625719` (executable, local Linux)

Tooling: `g++ 12.2.0`, `-std=c++20 -O2 -Wall -Wextra -pedantic`; JUCE-based
suites via CMake/Ninja against JUCE `9.0.1` at pinned commit
`e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` (strict suite pin verified by
`vox_verify_pinned_source`). Commands are the harness recipes in §WAVEFOLD
plus the project’s `cmake -S . -B build-linux -G Ninja -DVOX_BUILD_SUITE=ON
-DVOX_STRICT_DEPENDENCIES=ON` flow; full `ctest` output for the CMake-driven
suites is recorded in the PR response text accompanying this checkpoint.

| Suite | Result |
|---|---|
| FxNonlinearAliasTests (+ ASan/UBSan) | `0 failure(s)` |
| FxEffectsTests / FxSoundGraphTests / FxEffectTailTests / FxMacroRoutingTests | `0 failure(s)` each |
| FxProgramCodecTests (v10 migration) | `0 failure(s)` |
| FxPortablePresetTests / FxGeneratorTests / FxGeneratorStateTests | `0 failure(s)` each |
| FxSourceSoundTests / FxOfflineRenderTests / FxProgramStoreTests / FxPlaybackModeTests / FxBarEventTests | `0 failure(s)` each |
| Pattern ×4 / Factory candidates / Sample store·reference·granular / Transport / CurveTrajectory / FamilyDifferentiation | `0 failure(s)` each (11 suites) |
| JUCE-dependent suites (FileExport, SampleDecode, Editor…) | via CMake `ctest` — see PR response |

Frozen-item spot checks at `1625719`: `parameters.csv` byte-identical to
parent; `products/fx/content/` byte-identical to parent; no file outside
`products/fx/engine`, `tests/suite`, `docs/ultima-vox-music/fx` changed.

## 6. Integrated Windows x64 engineering candidate — preparation receipt (task 6)

Per [INTEGRATED_CANDIDATE_PLAN_20261119](INTEGRATED_CANDIDATE_PLAN_20261119.md):

- **Exact source candidate**: `1625719fd5755fa1516d058b0b272a963aec33c9`
  (Arena product branch). Branch proposal per TL: a separate product
  integration branch cut from exact PR #156 head `5c2f122` receiving these
  slices as new commits — **no history rewrite** — plus the tooling-only
  snapshot branch at `ef0f975` carried as-is.
- **Preflight executed (parse-only, no PowerShell execution, no CI)**:
  `python3 .github/scripts/prepare-fx-windows-preflight.py /tmp/fx-preflight-probe`
  → `Checkout 1625719…; 16 PowerShell run blocks extracted, not executed;
  SuiteIdentity.cmake matches workflow bundle names: VOX Trance Synthesis,
  VOX Drums, VOX FX`. The Windows-side `Test-FxWindowsPreflight.ps1` still
  requires Windows PowerShell 5.1 on the intended runner (not run).
- **Build definition (from `.github/workflows/suite.yml`, not run here)**:
  `cmake -S . -B build -G Ninja -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF
  -DVOX_STRICT_DEPENDENCIES=ON` on windows-2022 with Release CMake preset,
  then `ctest --test-dir build -C Release -L suite`, followed by the
  Steinberg validator step and the required-inventory check over the three
  `*.vst3` bundles (`VOX FX.vst3` first).
- **Provenance required in the receipt**: exact source SHA, JUCE
  `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, VST3 SDK
  `v3.8.0_build_66`, CMake/Ninja/MSVC versions, bundle inventory SHA-256s,
  validator log, CTest log.
- **Status: PREPARED, NOT DISPATCHED.** Queue contention and the standing
  HOLD mean no Windows CI is triggered from this branch; dispatch requires
  separate authorization.

## 7. Release evidence ledger (§21.4)

The exact-HEAD matrix remains the
[ENGINEERING_REQUIREMENT_MAP_20261122](ENGINEERING_REQUIREMENT_MAP_20261122.md)
table (FX-T01…FX-T20). At this checkpoint:

- **FX-T04** (if tracked as nonlinear acceptance): local Linux evidence now
  complete per WAVEFOLD doc; Windows/audio/manual columns remain
  `open`.
- **FX-T17** (codec/FPRG): local migration evidence complete; Windows column
  open.
- **FX-T18**: unchanged — prior deadline misses stand; no new measurements.
- **FX-T20**: unchanged — no validator run at any SHA in this checkpoint.
- Every other row: unchanged from the requirement map; no row is promoted to
  qualified by this checkpoint.

`RELEASE_LEDGER.md` carries the dated entry for this tranche.

## 8. What this checkpoint does *not* claim

No Windows execution, no CI dispatch, no PR #170 change, no merge into
`main`, no release, no listening approval, no production receipt, no
catalog/program approval (232 frozen), no export-policy change, no parser
change, no host parameter ID change, no silent schema change.
