# Proposal — ONE exact-source Windows x64 engineering run — 2026-11-27

**PROPOSED / NOT AUTHORIZED / NOT DISPATCHED. Draft / HOLD / NO MERGE / NO
RELEASE.** Prepared per TL comment
[#7212562366](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7212562366)
after acceptance of checkpoint `8ebdec0` for Windows engineering
preparation. This proposes a single engineering run; it is independent of
FX-T18 production ETW/CPU qualification (no live capture, no full timing
matrix). Existing #156/#170 HOLD/NO MERGE stands.

## 1. Exact source

- **Proposed source: `8ebdec0a9a3645d9cdf7907fe0b6216eb5b62f85`** —
  independently verified by the TL (two commits ahead of `2e6b0c70`,
  11 changed files; #170 remains `2e6b0c7`). The branch already descends
  from #156; no cherry-pick/rebase is needed to reconstruct lineage.
- This correction tranche adds documentation and one non-DSP `amountHelp`
  string (`git diff 8ebdec0..<tip> -- products/fx` shows only that line).
  The owner/TL may pin `8ebdec0` verbatim or the corrected tip; **the chosen
  SHA is recorded in the run receipt before dispatch**, and the workflow
  source-verification step must assert `git rev-parse HEAD` equals it.
- Dependency pins (unchanged): JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`
  (9.0.1); VST3 SDK `v3.8.0_build_66` /
  `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` with submodule pins; fetched
  via `Use-PinnedCheckout.ps1` — no moving tags, no stale copies.

## 2. Runner — HAMUNAPTRA self-hosted only

- **One run on the HAMUNAPTRA self-hosted Windows x64 runner. No
  github-hosted fallback.** If runner selection would resolve to
  `windows-2022` (or the HAMUNAPTRA inventory is unreachable/unapproved),
  **abort the run** rather than fall back.
- Record runner identity from the four approved HAMUNAPTRA runner-service
  identities, Windows version, CPU/logical cores, RAM, disk, power plan,
  MSVC/Windows SDK/CMake/CTest versions and runner load (per
  [WINDOWS_ENGINEERING_GATE_20261119](WINDOWS_ENGINEERING_GATE_20261119.md)).
- x64 CDB from Windows Debugging Tools must be installed for failure
  diagnostics; fixture-only workspace, no personal project state.

## 3. Preflight (read-only, actual PowerShell 5.1)

On the intended HAMUNAPTRA runner, from the trusted checkout of the pinned
SHA, before requesting authorization:

```powershell
$probe = Join-Path $env:TEMP ('vox-fx-preflight-' + [Guid]::NewGuid().ToString('N'))
python .github/scripts/prepare-fx-windows-preflight.py $probe
if ($LASTEXITCODE -ne 0) { throw 'FX preflight extraction failed' }
powershell.exe -NoProfile -File .github/scripts/Test-FxWindowsPreflight.ps1 -PreparedDir $probe
if ($LASTEXITCODE -ne 0) { throw 'FX PowerShell 5.1 preflight failed' }
```

Record PowerShell version, exact SHA, parser/inventory output (three dummy
bundles accepted; each missing name rejected; missing x64 binary rejected).
Extraction-only checks already ran on Linux at `8ebdec0` (16 blocks,
bundle names match `SuiteIdentity.cmake`); **Linux static checks are not
this receipt** — the 5.1 run is mandatory.

## 4. The run (exact `.github/workflows/suite.yml` steps, nothing skipped)

1. **Source verify**: `git rev-parse HEAD` equals the pinned SHA (extend
   the existing verify step through CI routing coordination if the event
   does not carry that SHA — see §6).
2. **Configure** (clean, no reused tree):
   `cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:JUCE_DIR" -DVOX_FX_DIAGNOSTIC_SYMBOLS=ON`
   (Visual Studio 17 2022).
3. **Build**: `cmake --build build-suite --config Release --clean-first --parallel`
   — a fresh `build-suite` directory plus `--clean-first` guarantees no
   stale binaries.
4. **Full suite CTest**:
   `ctest --test-dir build-suite -C Release -L suite --show-only=json-v1 > inventory.json`
   then
   `ctest --test-dir build-suite -C Release -L suite --no-tests=error --timeout 180 --output-on-failure --output-junit results.xml`.
   **No skip is acceptable**: compare inventory count vs executed count,
   require zero skipped/not-run, retain `LastTest.log` and
   `LastTestsFailed.log` if present.
5. **Steinberg validator — all three bundles** (`VOX FX.vst3` required,
   plus `VOX Drums.vst3`, `VOX Trance Synthesis.vst3`): per-bundle exit
   status and output. A validator skipped after a CTest failure is **not**
   validation; record pass / failed assertion / crash (exception code) /
   timeout / skipped / not run / missing evidence separately.
6. **Failure diagnostics**: on any failure, replay only the historical FX
   processor cases that failed (`suite_fx_noise_riser_tests`,
   `suite_fx_processor_tests`, `suite_fx_layer_tests`,
   `suite_fx_shepard_tests`) via `Capture-FxProcessorException.ps1`, and
   capture x64 CDB dumps/exception codes for crashes; retain under
   `suite-diagnostic`.
7. **SHA manifests**: source SHA + tree hash; JUCE/SDK pins; MSVC, Windows
   SDK, CMake, CTest, Python, PowerShell versions; SHA-256 of each built
   `*.vst3` binary and of every downloaded artifact (compare the service
   digest too); run URL and attempt; JUnit + text logs + validator logs +
   inventory.json. Empty or unavailable artifacts are not a pass.
8. **Installable artifacts if available**: upload the three bundles as
   labelling **diagnostic-only** unless the validator ran successfully on
   them; no installer, no release publication, no DAW use before hash
   check.

## 5. Explicitly out of scope

- FX-T18 production ETW/CPU qualification: no live capture, no full
  measurement matrix, no wall-gate decision — FX-T18 stays as recorded.
- Listening/owner acceptance (FX-T20), §20.2 catalog decisions, RC/merge/
  release, PR #170 changes.
- The single-processor marker `[RUN_FX_PROCESSOR]` — **not** added.

## 6. Dispatch prerequisites (all required)

1. **Explicit owner authorization** for this run at the pinned SHA — this
   proposal alone dispatches nothing.
2. **CI routing coordination** before any PR creation or ref move; the
   existing suite trigger (one deliberate addition of `[RUN_FX_SUITE]` to
   PR #156’s description, or a coordinated equivalent that makes the
   workflow check out the pinned SHA) must be agreed with TL/owner first.
   Do not run an older queued check; ordinary draft synchronization stays
   skipped.
3. HAMUNAPTRA runner availability confirmed; preflight §3 passed on the
   actual runner.

## 7. Receipt contents (when the run happens)

Preflight output · pinned SHA and tree · runner identity/versions ·
configure/build/ctest/validator step exit statuses · inventory vs executed
counts (zero skip) · per-bundle validator results · binary and artifact
SHA-256 manifests · CDB/diagnostic bundle on failure · JUnit/text logs ·
explicit statement that FX-T18 and release gates remain open.
