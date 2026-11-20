# VOX FX — one Windows engineering gate (not authorized yet)

[TL execution plan](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7032985337) · Draft #156 · **HOLD / NO MERGE**. This can qualify an engineering candidate, not a content-complete release. Neither the suite marker, processor-triage marker, nor dispatch was used in staging.

## Preflight and one run

1. TL reviews the staging package, then integrates only the reviewed SHA to Draft #156. Owner explicitly authorizes **that** SHA. Verify the remote draft head and base `next/standalone-suite`; do not run an older queued check. The existing edited-body opt-in is one deliberate addition of the FX suite marker, not a general workflow dispatch. Do not add the separate single-processor marker. Ordinary Draft synchronization stays skipped.
2. Confirm an available trusted self-hosted Windows x64 runner, VS 2022 Desktop C++ workload/Windows SDK, native x64 compiler, CMake/CTest ≥3.22, fixture-only test projects (no customer assets), writable runner tool cache with enough build/diagnostic space, Git, and installed **x64 CDB** from Windows Debugging Tools. Record Windows version, CPU, logical cores, RAM, disk/filesystem, power plan, MSVC/CMake versions and runner load. Separate build/test from foreground host UI on suitable test machines. Keep dump access restricted; no personal project state in crash fixtures.
3. `.github/workflows/suite.yml` now checks out the actual PR head for this FX gate, compares `git rev-parse HEAD` to the event head and records actual commit/tree separately from the event SHA. Re-verify JUCE 9.0.1 `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8` and Steinberg validator SDK `v3.8.0_build_66` / `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` including SDK submodule pins. Use `Use-PinnedCheckout.ps1`; do not silently accept stale or moving tag copies. Build validator from the SDK with examples and VSTGUI off. Build-time JUCE bundles and standalone validator are recorded separately.

### Read-only preflight before requesting authorization

On the intended Windows runner, from the trusted reviewed checkout, use Python 3 with PyYAML, CMake, and **Windows PowerShell 5.1**. Keep the output private. Use a fresh scratch path; these commands do not build, start a job, invoke a validator, replay CDB, or change any opt-in.

```powershell
$probe = Join-Path $env:TEMP ('vox-fx-preflight-' + [Guid]::NewGuid().ToString('N'))
python .github/scripts/prepare-fx-windows-preflight.py $probe
if ($LASTEXITCODE -ne 0) { throw 'FX preflight extraction failed' }
powershell.exe -NoProfile -File .github/scripts/Test-FxWindowsPreflight.ps1 -PreparedDir $probe
if ($LASTEXITCODE -ne 0) { throw 'FX PowerShell 5.1 preflight failed' }
```

The extraction evaluates `SuiteIdentity.cmake` via `cmake -P`, compares it against the **whole** expected-name list, and saves the unexecuted PowerShell blocks. The 5.1 parser must accept every block and the two helper scripts. The isolated dry run accepts the three dummy bundle paths, rejects each missing name independently and rejects a missing `Contents/x86_64-win/<bundle>.vst3` file. Real x64 bundles and validator data are required separately. Record PowerShell version, exact SHA, parser/inventory output and any mismatch before asking the owner. Linux static checks are not this receipt.

The automated steps (not commands to run from this sandbox):

```powershell
cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:JUCE_DIR" -DVOX_FX_DIAGNOSTIC_SYMBOLS=ON
cmake --build build-suite --config Release --clean-first --parallel
ctest --test-dir build-suite -C Release -L suite --no-tests=error --timeout 180 --output-on-failure --output-junit suite-out/ctest/results.xml
# On CTest success, run the SDK Release validator on all three built bundles.
& $env:VST3_VALIDATOR '<built VOX FX.vst3 bundle directory>'
```

CI uses absolute JUnit paths; record each exit status. The job timeout is 120 min; 180 s is the per-test CTest default for this gate, not a deadline for sound or a retry allowance. Keep `LastTest.log`, `LastTestsFailed.log` if present, JUnit, `ctest -N` inventory, per-bundle validator output, exact binary SHA-256 and downloaded artifact SHA-256 (compare the service digest too), compiler/SDK/run URL and fixture-only golden provenance. The FX gate records hashes from runner-local golden files; it does not upload those WAVs. Do not co-locate the frozen listening exports in the build workspace. Require the expected VOX FX, VOX Drums and VOX Trance Synthesis bundles. Record **pass / failed assertion / crash (exception code) / timeout / skipped / not run / missing evidence** separately. A skipped validator after CTest failure is not validation. The FX manifest records the CTest and validator step outcomes explicitly and labels binaries diagnostic-only unless the validator ran successfully. Empty or unavailable artifacts are not a pass. Download and hash-check a candidate before using it in a DAW. No installer or release publication here.

## Historical crash audit

Older [Windows runs](STATUS.md) observed four crashes in Noise Riser, Processor, Layer and Shepard. `01d8b41` moved large engine/processors off the test stack; it cannot prove the same fault explained all four. Later run [`44479129114`](evidence/windows-44479129114.txt) recorded **73/74**, processor `SEGFAULT` and skipped validation. After diagnostic triage, `71c5045` moved large editor fixtures to heap in response to the `0xC00000FD` `__chkstk` finding recorded in that commit; the historical downloaded-evidence limitation remains recorded in [`44497593788`](evidence/windows-44497593788.txt). Current fixtures use heap for those engine/editor objects; Linux 1 MiB-stack regressions are not current MSVC/Windows results.

If one of the four fails in the authorized run, replay its same executable and working directory from `ctest --show-only=json-v1` with matching optimized Release PDBs, installed CDB and a **90 s** replay bound (job step at most 8 min for four cases). The allowlisted `Capture-FxProcessorException.ps1 -TestName <ctest-name>` saves second-chance AV/stack-overflow/fail-fast evidence, transcripts, EXE/PDB/dump hashes where available, without changing WER or attaching to a host. It does not override CTest. CDB enables exact symbols and logs the named executable’s loaded-symbol status. Treat an adjacent, hashed PDB as a candidate, not proof of a load/match. Missing debugger, mismatched or unloaded symbols, CDB timeout, replay success or missing dump leave the original result unresolved.

## Same-binary controlled qualification

| Group | Test and acceptance evidence |
|---|---|
| Loaded FX-T18 | Run the [routed, authored and typical captures](CANDIDATE_GATE_20261119.md#capture-thread-cpu-without-changing-the-windows-gate) plus the same binary under documented representative load, at 44.1/48/96 kHz × 64/128/512 × 1/4/8 voices with Pattern off/on for stress. Include 96k/64/1 and 44.1k/512/8 Pattern. Retain raw miss rows, wall max/p99 and thread CPU, valid/min-positive/zero deltas, power/load/affinity, allocator and lock probe availability, and budget in µs = `frames / rate × 1e6`. No measured budget exceedances or callback allocation/blocking qualify a pass; coarse `GetThreadTimes` needs suitably resolved thread-running evidence (approved trace), not interpreting zero deltas as zero CPU. Follow with real host load including Drums + Synth. |
| Win32 Save | Real chooser, cancel/reopen, create-only refusal (selected existing and late winner), explicit distinct filename, non-ASCII, spaces, invalid/long paths, unreadable/locked/read-only/ACL-denied destinations, reparse leaf, parent path, controlled full disk, absent-target collision, cancellation before claim and refusal to cancel after claim, no copy fallback. Compare original/stage/final sizes and hashes. Treat network/cloud as out of guarantee. |
| Win32 drag | Actual copy-only OLE WAV and SMF in Cubase and REAPER; same-path retries, cancel, duplicate/reentered/late callbacks, new preparation, Copy into project vs reference, reopen and unload. Confirm MIDI remains editable and file remains readable after unload. Do not evict offered scratch. |
| Host, UI and bounce | Validate six modes, repeat notes, real PPQ/meter/seek/start/stop, 4/8/16-bar Transition target, state/A-B/sample relink, automation IDs, focused Motion undo and layer-drag restore, keyboard-only curve insertion/selection/undo, Tab/focus and screen readers, 100/125/150/200% DPI, and matching-history wet-tail bounce vs fresh start. Exercise editor close/reopen, processor destroy, cancellation/read-only/blocked I/O; record stalls. |

Engineering qualification and the **one owner Cubase/REAPER round** are distinct. Owner listening/rights review and **600 distinct owner-approved factory programs** remain separate production gates. The current library is frozen at **232 review-only / 0 approved**.

## Save policy: owner-approved create-only

The prior overwrite proposal is closed. Validate the fixtures and real chooser/host cases in [Create-only Save](CREATE_ONLY_SAVE_20261119.md). Use only disposable local paths. No change to `.voxfx` preset editing; no automatic removal of offered drag files. The Windows gate is not authorized by this policy decision.
