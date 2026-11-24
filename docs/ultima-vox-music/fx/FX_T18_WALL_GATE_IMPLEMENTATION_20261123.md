# FX-T18-WALL-IMPL-01 implementation record

**Status:** implementation on PR #170; this checkpoint is source remediation
pending owner review. Windows execution, full-matrix dispatch, owner
attestation, qualification, merge and release acceptance remain separately
blocked.

This change keeps production DSP, state, transport, UI, host contracts and all
existing timing thresholds unchanged. It adds the functional TSV/stdout record,
strict evaluator/conformance fixtures, a separately verified host-attestation
contract, and a safe host-observation schema. No owner proof or live positive
receipt is fabricated.

## Implemented paths

- `tests/suite/FxRuntimeStressTests.cpp` — one functional record per case with
  the six assertion bits, derived pass and canonical reason codes; existing
  assertions, counters, raw wall/CPU/IAT data, positive controls and legacy
  output remain.
- `evidence/audit-fx-windows-timing.py` — strict functional parser and exact
  case reconciliation, with optional fixture-mode retention of failed probe
  coverage as instrumentation/evidence blockage. Native CTest/JUnit
  reconciliation models the real `#78` coarse-clock capture: one prefixed and
  one CTest-echoed CPU marker, a failed JUnit testcase with `message="Failed"`,
  and the allowlisted terminal summary; unknown mixed native reasons remain
  blocked.
- `evidence/evaluate-fx-t18-wall-gate.py` — three-profile evaluator for
  54 routed + 54 authored + 9 typical cells, independent dispositions,
  provenance/manifest hooks, host-witness validation and the required
  `WALL_GATE_FAIL` → `WALL_GATE_BLOCKED` →
  `WALL_GATE_PASS_CPU_UNKNOWN` precedence. Partial functional salvage retains
  only internally consistent observed violations.
- `evidence/test-audit-fx-windows-timing.py` and
  `evidence/test-evaluate-fx-t18-wall-gate.py` — executable parser/status
  fixtures, including the real `#78` CTest/JUnit ending, unrelated native
  reason/listing negatives, malformed functional evidence and truncation
  salvage.
- `evidence/capture-fx-t18-host-witness.ps1` — safe Windows PowerShell 5.1
  observation-only phase with owner-pinned policy/lease CMS chain/revocation checks, four-service
  time-bound identity baseline, deny precedence, strict path/hash/parent/service
  policy, dynamic/PID-reuse reconciliation, one global monotonic deadline
  origin, private raw inventory and public counters/hashes. It has no
  service/process control operations, no public command-line serialization, and
  never requires future ETW/CTest facts.
- `evidence/finalize-fx-t18-host-witness.py` — post-capture-only finalizer that
  verifies the immutable observation, independently pinned detached proofs,
  parsed ETW/WMI event equality, PID lifecycle, exact CTest intervals and
  private-inventory hash before producing the sole production witness.
- `evidence/evaluate-fx-t18-wall-gate.py` — second-stage hash/CMS verification
  on the owner host, host-manifest reconciliation for every payload, detached
  signature, collector, runner receipt and private inventory, and fail-closed
  rejection of fixture/qualification-prohibited evidence.
- `evidence/test-fx-t18-host-witness.ps1`,
  `evidence/test-evaluate-fx-t18-wall-gate.py`, and
  `FX_T18_HOST_WITNESS_V1.md` — executable missing/forged-proof, WMI-loss,
  dynamic/PID-reuse, DAW/manual-build/other-runner, short-lived-competitor,
  cadence, phase/CTest, hash and qualification-prohibited fixtures; all raw
  inventory remains private or synthetic.
- `FX_T18_HOST_ISOLATION_OWNER_RUNBOOK.md` — minimal separately authorized
  owner procedure for exclusive HAMUNAPTRA access, independent attestations,
  future Capture, cleanup and retention.

## Exact sandbox commands and results

All commands below were run from the repository root on Linux. None dispatched
Windows or ran the full 117-cell matrix.

```text
PYTHONDONTWRITEBYTECODE=1 python3 docs/ultima-vox-music/fx/evidence/test-audit-fx-windows-timing.py
FX-T18 auditor negative fixtures: PASS

PYTHONDONTWRITEBYTECODE=1 python3 docs/ultima-vox-music/fx/evidence/test-evaluate-fx-t18-wall-gate.py
FX-T18 wall-gate executable fixtures: PASS
fixtures: two_phase_observation_finalization, independent_host_attestations, fake_attestation_cannot_production_pass, etw_competitor_absent_from_wmi, mixed_name_same_pid_event, independent_owner_pin_mismatch, strict_policy_wildcard_path, strict_policy_missing_service_identity, strict_policy_missing_dynamic_authorization, strict_policy_deny_allow_conflict, strict_policy_wrong_executable, capture_sampler_global_monotonic_clock, coarse_cpu_plus_functional, all_profiles_unverified_provenance, functional_reason_mismatch, non8_voice_invariant, missing_functional_record, missing_rows_or_manifest, failed_iat_positive_control, observed_lock_wait, timeout_or_skip, observed_wall_then_crash, functional_failure_then_truncation, absent_native_outcome, ctest_nonzero_unrelated, junit_failure_green_tsv, cpu_claim_unrelated_raw_junit, cpu_claim_mixed_raw_junit, cpu_claim_other_test_listing, cpu_claim_selected_executable_mismatch, mismatched_native_outcome, three_sample_witness, witness_missing_ctest_interval, preexisting_daw, other_runner_identity, manual_build_identity, daw_workload_identity, forged_policy_flag, nonlisted_build, forged_lease, missing_event_time_coverage, short_lived_competitor, dynamic_pid_reuse_mismatch, phase_ctest_mismatch, fixture_mode_qualification_prohibited, missing_owner_proof, forged_owner_signature, unverified_wmi_loss, missing_manifest_reconciliation

python3 -m py_compile docs/ultima-vox-music/fx/evidence/audit-fx-windows-timing.py docs/ultima-vox-music/fx/evidence/evaluate-fx-t18-wall-gate.py docs/ultima-vox-music/fx/evidence/finalize-fx-t18-host-witness.py docs/ultima-vox-music/fx/evidence/test-audit-fx-windows-timing.py docs/ultima-vox-music/fx/evidence/test-evaluate-fx-t18-wall-gate.py
# exit 0; __pycache__ removed immediately afterward

git diff --check
# exit 0
```

The focused native build/test command was:

```text
bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh build/fx-t18-impl-validation
# exit 0
# FX processor: 0 failure(s)
# FX runtime stress: 0 failing cases
# FX file export: 0 failure(s)
# FX automation contract: 0 failure(s)
```

The native typical schema smoke was:

```text
out="$PWD/build/fx-t18-impl-validation"
head=$(git rev-parse HEAD)
tree=$(git rev-parse HEAD^{tree})
sha=$(sha256sum "$out/FxRuntimeStressTests" | awk '{print $1}')
VOX_FX_STRESS_TYPICAL=1 VOX_FX_TIMING_THREAD_CPU=1 \
VOX_FX_REPORT="$out/typical-schema.tsv" \
VOX_FX_SOURCE_SHA="$head" VOX_FX_SOURCE_TREE="$tree" \
VOX_FX_EXECUTABLE="$out/FxRuntimeStressTests" \
VOX_FX_EXE_SHA256="$sha" \
VOX_FX_RUNNER_RECEIPT_SHA256="$(printf fixture | sha256sum | awk '{print $1}')" \
"$out/FxRuntimeStressTests" > "$out/typical.stdout.txt"
# exit 0; 9 case rows, 9 functional rows, 1,440 block rows, zero miss rows

PYTHONDONTWRITEBYTECODE=1 python3 docs/ultima-vox-music/fx/evidence/audit-fx-windows-timing.py \
  build/fx-t18-impl-validation/typical-schema.tsv \
  --allow-nonwindows --allow-unverified-provenance > build/fx-t18-impl-validation/typical.audit.json
# exit 0; profile=typical cells=9 callbacks=1440 functional_failures=0
# performance_failure=False cpu_measurement_insufficient=False
```

The combined Windows evaluator is invoked only after the three independent
reports, receipts, manifests and the host witness exist:

```text
python docs/ultima-vox-music/fx/evidence/evaluate-fx-t18-wall-gate.py \
  --report routed=routed.tsv --report authored=authored.tsv --report typical=typical.tsv \
  --receipt routed=routed.runner.json --receipt authored=authored.runner.json --receipt typical=typical.runner.json \
  --manifest routed=routed.artifacts.json --manifest authored=authored.artifacts.json --manifest typical=typical.artifacts.json \
  --root . --executable suite_fx_runtime_stress_tests.exe \
  --expected-head OWNER_APPROVED_HEAD --expected-tree OWNER_APPROVED_TREE \
  --witness host-witness.json --host-manifest host-artifacts.json \
  --owner-certificate-thumbprint OWNER_SUPPLIED_40_HEX_THUMBPRINT
```

The native run and schema smoke are Linux implementation validation only. The
PowerShell 5.1 scripts were added but not dispatched in this Linux sandbox;
Windows execution remains unresolved and separately unauthorized. The
historical red run `45130013544` remains unchanged and is not relabeled.
