# FX-T18 production qualification plan

**Status: owner-reviewable plan only — HOLD / NO MERGE / NO RELEASE**

This document defines one future, owner-authorized production qualification
sequence. It is not an execution request, an approval, a `WALL_GATE_PASS`, or a
release certificate. The agent has not dispatched a Windows runner, started a
live ETW capture, run the full matrix, installed a DAW, or performed a DAW
round.

The retained HAMUNAPTRA compatibility replay is recorded in the
[host-isolation runbook](FX_T18_HOST_ISOLATION_OWNER_RUNBOOK.md#6-retained-artifact-offline-replay-acceptance-record).
It closes the diagnostic decoder-compatibility gate only. Its observed
`5/4/9` process shape and diagnostic `5/3/8` reconciliation are not production
parser evidence, because production parsing remains fail-closed on the one
unmatched stop.

## 1. Entry decision and immutable inputs

An owner must explicitly authorize the exact source and the exact Windows
runner before any execution. The owner records the following values in the
private evidence manifest; placeholders are deliberate and must not be filled
by inference:

| Input | Required recorded value | Acceptance rule |
|---|---|---|
| Source | owner-approved exact commit and tree SHA | checkout is detached at that exact commit; the job reports `git rev-parse HEAD` and tree; no stale artifact or moving branch is accepted |
| Base/tool revisions | JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`; Steinberg VST3 SDK `v3.8.0_build_66` / `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` | all submodule/content pins reconcile to the reviewed source record |
| Parser | final reviewed source `cf67051aadec802223c49819c72ed264c1eac7cc`; implementation `b79d517b5e6a5bc11168b3331956876126c3e42a`; version `1.11.0` | parser is frozen after the owner-verified retained replay; any future change requires a demonstrated defect and a new owner-authorized replay |
| Windows SDK decoder | versioned contract decision below; current source pin is `10.0.26100.1` | no production run may use the measured serviced build until the owner approves the explicit contract amendment and its exact hashes/CMS record |
| ETW collector | `C:\Windows\System32\logman.exe`, owner-approved normalized version `OWNER_SUPPLIED_LOGMAN_VERSION`, exact SHA-256 `OWNER_SUPPLIED_LOGMAN_SHA256` | exact path, version, bytes and Microsoft Authenticode match the same approved tool record |
| Approval record | `tracerpt-identity-approval.json` plus detached CMS and independent verification receipt | detached CMS chain/revocation and owner thumbprint verify the exact tracerpt/logman paths, versions and hashes; JSON booleans are not proof |
| Host runtime | Windows PowerShell Desktop 5.1, Visual Studio 2022 x64 C++, Windows SDK, CMake/CTest `>=3.22`, Git, x64 CDB | exact versions, paths, signatures/hashes where applicable, and runner load are retained before execution |
| Runner/host | owner-selected isolated HAMUNAPTRA and intended Windows x64 runner | complete baseline, lease and private process inventory cover the whole pre/during/post interval; no unlisted DAW, runner, build or audio workload is present |

The owner must preserve the approval JSON, detached CMS signature, independent
CMS verification receipt, certificate thumbprint, tool identity inventory and
source/tool manifest. The parser must receive the owner-approved exact
tracerpt SHA-256; it may not discover a hash and approve it itself.

### 1.1 Versioned production tool-contract decision

The retained compatibility run genuinely measured and tested the serviced
System32 decoder `C:\Windows\System32\tracerpt.exe` at normalized version
`10.0.26100.9949`, with tracerpt SHA-256
`aee3a047976f60e69357eb415d0d69b7b38325a55de67afa77d5b3fd5c28777b`. The same
owner review recorded `C:\Windows\System32\logman.exe` SHA-256
`5376695e8ecdeb536b7cede4ba7d10ce6dfe07a51aa7c9651911d2c3cb66840d`; its
normalized FileVersion must be measured again and entered below rather than
inferred. These are measured compatibility identities, not automatic
production approval.

Before any production authorization, the owner must choose one explicit
versioned contract and sign the choice:

| Contract | tracerpt | logman | Decision state |
|---|---|---|---|
| Existing source pin | `10.0.26100.1`, exact owner-approved SHA required | exact owner-approved path/version/SHA required | remains the parser policy until amended |
| Proposed serviced contract | `10.0.26100.9949`, SHA `aee3a047976f60e69357eb415d0d69b7b38325a55de67afa77d5b3fd5c28777b` | `C:\Windows\System32\logman.exe`, measured version `OWNER_RECONFIRMED_LOGMAN_VERSION`, SHA `5376695e8ecdeb536b7cede4ba7d10ce6dfe07a51aa7c9651911d2c3cb66840d` | proposal only; requires owner approval |

For either contract, the private approval record must bind both exact System32
paths, normalized four-part versions, SHA-256 values, Microsoft Authenticode
status and signer/thumbprint, owner certificate thumbprint, detached CMS
signature, chain/revocation result and independent CMS verification receipt.
The receipt must bind the approval JSON and signature hashes to the same tuple.
No version-family match, PATH resolution, hash discovery, binary downgrade or
self-approval is allowed. The proposed serviced contract cannot be used until
its logman version, Authenticode details and CMS approval are populated and
independently verified.

## 2. Preflight and signed boundary

Before a capture or build, the owner performs the read-only preflight in the
[host-isolation runbook](FX_T18_HOST_ISOLATION_OWNER_RUNBOOK.md), including:

1. Verify the exact source checkout and dependency pins.
2. Verify the detached CMS approval and retain a receipt binding the approval
   JSON hash, signature hash, certificate thumbprint, exact tool paths,
   normalized versions and SHA-256s.
3. Verify the complete four-service runner baseline, all permitted child
   identities, deny rules, host lease and private inventory path. An unknown
   PID, name, path, parent, service identity, manual build, DAW or unrelated
   test is a block, not a normal-process exception.
4. Run the PowerShell 5.1 AST and dummy-bundle preflight. It must not invoke a
   validator, build, runner job or host workload.
5. Record free space, power plan, logical CPU count, RAM, Windows build,
   compiler, CMake/CTest, SDKs, runner load and private evidence directory.

No future event-loss proof or execution binding may be pre-signed. No service
or process may be stopped, killed, disabled, restarted or reconfigured to make
the boundary pass.

### 2.1 Evidence coverage and workload binding

A preflight capture cannot attest a later build or timing workload. The owner
must bind each artifact to the interval and binary it actually covers:

| Phase | What it covers | Required binding |
|---|---|---|
| Build | clean exact-head checkout, dependency/toolchain, Release outputs and validator inputs | source HEAD/tree, build flags, selected CTest executable path, executable/bundle hashes and build receipt |
| Exclusive measurement | host-wide process boundary and the ETW interval surrounding the measured runner workload | the same host lease, complete four-service baseline, private inventory, ETW start/stop receipts, ETW identity join and execution binding; all four runner services are verified, not inferred from three visible active services |
| Synthetic timing profiles | routed `54`, authored `54`, typical `9` cells, `160` callbacks per cell | the same selected executable, profile receipt, raw TSV, CTest/JUnit outcome, auditor output and exact interval for each profile |
| Loaded timing | actual plugin workload with Drums and Synth representative load | separate host/DAW or approved loaded-host receipt, same binary SHA and interval-bound witness; synthetic callback evidence cannot stand in for it |
| DAW round | real Cubase and REAPER installation, transport, state, Save/drag, bounce and UI scenarios | host/version, bundle SHA, project/settings, reviewer, row-level result and private raw evidence |

The production ETW capture, identity join, loss proof and execution binding must
cover the actual timing workload, not merely a setup probe. If any interval,
source/tree, executable hash, lease, process identity or profile receipt is
not one-to-one, the affected result is `BLOCKED`.

## 3. Production ETW and identity gate

Only after the owner authorizes the exact run may the owner collect one
non-circular, zero-loss ETW interval with the pinned `logman.exe` command from
the runbook. Retain the raw ETL, exact start/stop control receipts, collection
metadata, independent file inventory and original cleanup/HRESULT evidence.
Then decode exactly once with the approved `tracerpt.exe` and retain its XML,
`-summary` output, command argv and console diagnostics.

The production parser must use direct `System` provider name/GUID and EventID
identity, the validated EventTrace Header, exact lifecycle/provider/version/
opcode/PID/creation-time/Header gates, and strict canonical timestamps. Its
identity join requires a unique PID plus exact canonical 100-ns creation-time
match. Duplicate, ambiguous, conflicting, stop-before-start, later-start,
duplicate-stop, ExitTime-before-creation and unmatched-controlled joins are
blocks. An earlier creation time is only possible boundary-crossing evidence;
it is not proof that creation preceded ETW capture without independent coverage
proof.

The compatibility diagnostic is not a production fallback. It may report the
single unmatched noncontrolled stop separately, but it must set
`qualification_prohibited=true` and never create a production receipt. A
production run with an unmatched process stop remains blocked. The owner must
not turn the retained `5/4/9` inventory into a production receipt by editing
or regenerating JSON claims.

Required production ETW evidence before the timing gate:

- raw ETL, XML, summary, control receipts, collection metadata and private
  identity join with hashes and unchanged-input verification;
- approved toolchain path/version/hash/CMS provenance and exact command argv;
- EventTrace Header identity, capacity, mode, buffer/event loss, summary
  counters and independent collection interval reconciliation;
- one-to-one ETW/WMI process identity and lifecycle reconciliation, including
  exact 100-ns creation ticks, parent/path/hash/service ancestry and policy
  decision for every production event;
- production parse receipt replayable from retained bytes, with no diagnostic
  fields used as a qualification substitute;
- event-loss and execution-binding signatures issued only after all facts are
  immutable.

Any missing private artifact, changed hash, loss, ambiguous join, invalid
receipt, unsupported timestamp, cleanup uncertainty or coverage mismatch is
`BLOCKED`.

## 4. Exact-head Windows engineering gate

The owner runs the reviewed source once on the selected Windows x64 runner.
This follows the [Windows engineering gate](WINDOWS_ENGINEERING_GATE_20261119.md)
and retains the run URL and exact artifact hashes.

### 4.0 Owner-only execution order and commands

The following is a future owner-console workflow, not an authorization for this
branch or this agent to execute it. Substitute only values already present in
the owner-signed manifest; do not invent a source/tree or tool hash.

```powershell
# Already elevated Windows PowerShell Desktop 5.1 on the owner-approved host.
$sourceHead = 'OWNER_SIGNED_SOURCE_HEAD'
$sourceTree = 'OWNER_SIGNED_SOURCE_TREE'
$juceDir = 'OWNER_SIGNED_JUCE_DIR'
$buildDir = 'build-t18-owner'
$outDir = 't18-owner-out'

# Exact detached checkout and clean-source gate; never use a moving branch.
git fetch origin
git checkout --detach $sourceHead
if ((git status --porcelain=v1 --untracked-files=all).Count -ne 0) { throw 'source checkout is not clean' }
$actualHead = (git rev-parse HEAD).Trim()
$actualTree = (git rev-parse HEAD^{tree}).Trim()
if ($actualHead -cne $sourceHead -or $actualTree -cne $sourceTree) { throw 'source HEAD/tree mismatch' }

# Read-only PowerShell 5.1/dummy-bundle preflight.
$probe = Join-Path $env:TEMP ('vox-fx-t18-preflight-' + [Guid]::NewGuid().ToString('N'))
python .github/scripts/prepare-fx-windows-preflight.py $probe
if ($LASTEXITCODE -ne 0) { throw 'preflight extraction failed' }
powershell.exe -NoProfile -File .github/scripts/Test-FxWindowsPreflight.ps1 -PreparedDir $probe
if ($LASTEXITCODE -ne 0) { throw 'PowerShell 5.1 preflight failed' }

# Fresh Release build, CTest-selected executable identity, all three profiles,
# raw TSV/CTest/JUnit/outcome/manifest retention and independent audits.
& powershell.exe -NoLogo -NoProfile -NonInteractive `
  -File .\docs\ultima-vox-music\fx\evidence\run-fx-windows-t18.ps1 `
  -ExpectedHead $sourceHead -ExpectedTree $sourceTree -JuceDir $juceDir `
  -BuildDir $buildDir -OutputDir $outDir -Python python -TimeoutSeconds 180
$timingOrchestratorExit = $LASTEXITCODE
```

The orchestrator must retain one receipt per profile and attempt all three even
when an earlier profile fails. The expected synthetic profile inventory is
`54 + 54 + 9 = 117` cells and `117 × 160 = 18,720` callback rows. In addition,
`loaded_drums` and `loaded_synth` are distinct named loaded-host scenarios,
with their own declared settings, binary hash, host-bound witness, execution
interval and raw evidence; they must not be silently folded into the synthetic
count. The owner declares the exact callback/cell count for each loaded
scenario before execution. A timeout,
crash, partial TSV, missing audit or missing outcome remains a failure/block;
it is never replaced by a later successful profile. The production ETW command,
identity join and signed execution binding are then run for the same selected
binary and actual measured interval, using the exact tool contract chosen in
Section 1.1. The loaded-host and Cubase/REAPER rounds remain separate phases.

### 4.1 Build, tests and validator

Build a clean x64 Release configuration with the reviewed JUCE/VST3 inputs and
run the complete FX suite, not a focused substitute:

```powershell
cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON `
  -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON `
  "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:JUCE_DIR" -DVOX_FX_DIAGNOSTIC_SYMBOLS=ON
cmake --build build-suite --config Release --clean-first --parallel
ctest --test-dir build-suite -C Release -L suite --no-tests=error `
  --timeout 180 --output-on-failure
```

Run the Steinberg Release validator against all three required bundles: VOX
FX, VOX Drums and VOX Trance Synthesis. Record each validator exit status and
output, CTest/JUnit inventory and outcome, selected executable/bundle SHA-256,
downloaded artifact SHA-256, compiler/SDK versions and run URL. A skipped
validator, missing bundle, crash, timeout, failed assertion or missing raw log
is not a pass.

### 4.2 CPU method decision before full timing

The owner-authorized typical diagnostic established that `GetThreadTimes` can
quantize at `15,625` microseconds, which is coarser than the callback budgets.
That result remains `CPU_MEASUREMENT_INSUFFICIENT` and cannot be relabeled as
zero CPU or as a scheduler-only wall result. No full `117`-cell timing run is
entered until the owner chooses one of these explicit contracts:

**Option A — separately reviewed high-resolution CPU method (preferred if a CPU
claim is required).** Implement and review a Windows ETW/WPR scheduler trace
contract that:

1. marks callback boundaries with a no-allocation/no-lock correlation marker and
   identifies the measured thread unambiguously;
2. records scheduler running/context-switch intervals, timestamp mode,
   affinity, provider/version, buffers and lost-event counters;
3. calibrates trace timestamps to the wall bracket and intersects running
   intervals with each callback window, including intervals crossing a boundary;
4. validates resolution and attribution with a same-runner positive control;
5. marks CPU attribution `UNKNOWN` for missing markers, ambiguous identity,
   event loss, discontinuity or unresolved overlap, preserving all raw
   `GetThreadTimes` values.

The owner must approve the provider/event contract and review a bounded Windows
smoke showing complete correlation, zero loss and resolution materially below
the strictest callback budget before the full matrix. The existing
[CPU method proposal](FX_T18_CPU_METHOD_PROPOSAL_20261123.md) is the design
record; it is not an executed trace or an authorization.

**Option B — explicit wall-only diagnostic policy.** If the decision is callback
completion rather than attributable thread CPU, the owner may approve a
versioned wall-only contract in which wall deadline misses remain authoritative,
allocations/locks/waits remain hard safety failures, all raw CPU values and
`CPU_MEASUREMENT_INSUFFICIENT` remain visible, and CPU-based qualification is
prohibited. The policy must specify the same `117` cells, `18,720` callbacks,
loaded conditions, host boundary, wall threshold, raw-log retention and
`CPU_UNKNOWN` result. It is not a threshold relaxation and cannot be described
as CPU qualification or release approval.

### 4.3 Loaded FX-T18 timing and safety evidence

Using the same reviewed binary and the owner-approved host boundary, run the
routed, authored and typical captures. Cover
`44.1/48/96 kHz × 64/128/512 frames × 1/4/8 voices × Pattern off/on`, including
`96 kHz / 64 frames / 1 voice` and `44.1 kHz / 512 frames / 8 voices / Pattern`.
Include documented representative loaded conditions with Drums and Synth.

Retain raw miss rows, wall maximum/p99, thread CPU, valid/zero/minimum-positive
CPU deltas, exact per-miss configuration, power/load/affinity, allocation and
lock-probe coverage, host/runner samples, and the microsecond budget
`frames / sample_rate × 1e6`. The acceptance decision requires no measured
budget exceedance, callback allocation or blocking/lock/wait observation, and
sufficiently resolved thread-CPU evidence. Zero or coarse `GetThreadTimes`
deltas are not zero CPU. If timing resolution cannot assess the deadline, the
result is blocked and no wall pass is emitted.

The owner runs the executable timing auditor and preserves its output, but the
auditor cannot replace raw logs, signed host evidence, ETW loss proof or the
actual loaded-host round.

## 5. Actual Windows and DAW validation

Only after the exact-head build, validator, host boundary, production ETW and
loaded timing gates pass may the owner perform the real host round. Use scratch
projects/assets only; do not use customer projects or private media as test
fixtures. Test both Cubase and REAPER, recording host/version, plugin bundle
SHA, project/settings, reviewer and result for every row.

### 5.1 Installation, lifetime and state

- Install/load/uninstall the exact hashed VST3 bundles and confirm no stale
  bundle is selected.
- Open/close/reopen the editor; destroy/recreate the processor; cancel and
  recover; exercise blocked/read-only I/O and unload.
- Verify state save/restore, A/B, sample relink, automation IDs/readback,
  preset/program and pattern persistence, and no stale process/lease or host
  handle after teardown.
- Exercise real PPQ/meter/seek/start/stop, repeat notes, six modes,
  Transition targets of 4/8/16 bars, post-event tails/bar-clock behavior and
  matching-history versus fresh-start bounce.

### 5.2 Save, drag and exchange

- Use the actual OS chooser for create-only WAV/SMF Save: cancel/reopen,
  existing path refusal, late winner refusal, distinct filename, spaces and
  non-ASCII, invalid/long paths, locked/read-only/denied destinations,
  reparse leaf, parent path, low-space, absent-target collision and cancel
  before claim. Verify no replacement and no copy fallback.
- In both DAWs, perform actual copy-only OLE WAV and SMF drag/drop, same-path
  retry, cancel, duplicate/reentered/late callbacks, fresh preparation, Copy
  into Project versus Reference, reopen and unload. Confirm MIDI remains
  editable and the copied file remains readable.

### 5.3 UI, transport and audio acceptance

- Exercise keyboard-only focus/traversal, Source/Key and curve insertion,
  selection, undo and restore; check hidden controls are not tabbable.
- Check 100/125/150/200% DPI, focus indication, host shortcut isolation and
  screen-reader/accessibility behavior.
- Verify all required playback, modulation, routing, effect, granular,
  automation, bounce, tail, error and teardown scenarios from the candidate
  gate. Record stalls, hangs, audible failures, clicks, state loss and
  non-determinism, not only sample comparisons.

A clean validator or synthetic/offscreen test cannot substitute for this
actual host evidence. Any failed or unrun row blocks qualification.

## 6. Final decision package

The owner prepares one private manifest containing, at minimum:

- exact source/tree, dependency pins, build flags, runner/host/tool versions
  and approved tracerpt/logman paths, versions and SHA-256s;
- CMS approval JSON, detached signatures, independent verification receipt and
  certificate thumbprint;
- signed identity policy, lease, private inventory, observation/final witness,
  ETW loss proof, execution binding and production parse receipt;
- raw ETL/XML/summary, collection metadata, start/stop receipts, identity join,
  CTest/JUnit/raw timing logs, validator outputs, bundle/download hashes and
  DAW test ledger;
- parser/auditor source hashes and sanitized public summary.

Recompute all hashes after manifest creation and verify the manifest itself.
Publish no raw PID, process name, path, command line, service identity, private
ETL/XML/summary or customer project. The public result contains only sanitized
counters, decisions, intervals, approved identity/artifact hashes and blocked
reasons.

The final decision requires explicit owner sign-off for every gate:

```text
source_exact = OWNER_SIGNED
final_fx_dsp_source_lineage = OWNER_SIGNED
cms_toolchain = OWNER_SIGNED
tracerpt_contract = OWNER_SIGNED_VERSIONED_CHOICE
logman_contract = OWNER_SIGNED_VERSIONED_CHOICE
host_boundary_all_four_runner_services = OWNER_SIGNED
production_etw_receipt = OWNER_SIGNED_FOR_ACTUAL_WORKLOAD
production_identity_join = OWNER_SIGNED_FOR_ACTUAL_WORKLOAD
execution_binding = OWNER_SIGNED_FOR_ACTUAL_WORKLOAD
windows_build_ctest = OWNER_SIGNED
validator_all_three_bundles = OWNER_SIGNED
cpu_method = OWNER_SIGNED_OPTION_A_OR_WALL_ONLY_OPTION_B
fx_t18_loaded_timing = OWNER_SIGNED
cubase_round = OWNER_SIGNED
reaper_round = OWNER_SIGNED
product_readiness_pr156 = SEPARATE_OWNER_SIGNOFF
manifest_reconciled = OWNER_SIGNED
qualification_decision = OWNER_SIGNED
merge_release = PROHIBITED_UNTIL_SEPARATE_AUTHORIZATION
```

Until every value is replaced by owner-retained evidence and separately
approved, the project remains HOLD / NO MERGE / NO RELEASE. This plan does not
authorize a new capture, runner dispatch, full matrix, production approval,
merge or release.
