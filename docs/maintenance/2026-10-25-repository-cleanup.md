# Repository cleanup — 25.10.2026

## Result

Adopt current Trance Engine product direction and separate style-specific rack instruments.
Replace obsolete working documentation with product/status/roadmap/contracts/UI/build entry points.
Preserve inactive Kick/MATCH source and obsolete policy documents in an immutable-content archive.
Keep compiled instruments, compatibility code, CMake and CI unchanged.
Prepare a bounded AGY task for the reported Bass/generation failures.

## Verification

- 29 archive entries checked against SHA-256 and original Git bytes.
- 152 active runtime/build/CI files compared byte-for-byte with main baseline b076a53d8900b53433e8aa4fdb1027c8f2bed135.
- 84 active local documentation links resolved before adding this report.
- Active CMake source paths exist; no archive/legacy input in root CMake.
- git diff --check passed before publication.
- Independent read-only review: no BLOCKER/MAJOR; one inconsistent profile/instrument phrase corrected.
- Windows build/CTest/package: not run locally; use PR CI.
- DAW/audio/visual tests: not run; not required to validate this docs-only relocation, still required for functional work.

## Retention

Archived bytes are not rewritten to repair their historical links/status claims.
Compiled Acid, Lead, Atmos, Semantic FX, Parts and legacy parameter IDs are retained.
The runtime is still named Vox Electronic Engine.
Old issues #10/#11 are snapshotted so later superseding their roadmaps does not lose original requirements.
PR #43 remains separate pending actual workflow/visual review.

## AgentReport

```yaml
agent_report:
  role: Lead / Docs-Spec
  task: adopt Trance Engine documentation and archive obsolete material
  result: PASS
  files_changed:
    - README.md, AGENTS.md, agents current guidance
    - docs current product/status/roadmap/contracts/UI/build/task documents
    - archive indexes, immutable snapshots and manifest
    - libs/vox-ui/README.md
  contracts_touched:
    - product/documentation authority only; runtime contracts preserved
  assumptions:
    - preserve existing project compatibility until owner specifies otherwise
  implementation:
    - style-specific rack instruments share DSP
    - archive inactive source and superseded policy
    - isolate AGY Bass diagnosis from ABI/routing redesign
  tests_added:
    - none; no runtime change
  verification:
    - command: archive hash/Git-byte, doc-link and source-invariance audit
      result: PASS
    - command: git diff --check
      result: PASS
    - command: Windows Release build / CTest / package
      result: NOT_RUN
      notes: use required PR CI; Linux docs cleanup does not prove Windows results
  architecture_changes:
    - agreed product direction documented; no runtime architecture changed
  migration_impact:
    - none in code; existing IDs/state/build filenames preserved
  performance_impact:
    - none; active processing sources unchanged
  security_impact:
    - no new runtime dependency or network feature
  manual_acceptance_required:
    - none for docs cleanup; DAW/sound/visual acceptance remains for functional work
  unresolved:
    - required CI state and integration at publication time
    - owner hardware and old-project/piano/licensing questions
  risks:
    - historical archived docs retain obsolete wording and links by design
```

Merge recommendation at preparation: WAIT for required CI.
Manual gate for this docs-only cleanup: N/A.
Next task: AGY reproduces Bass/generation failures and delivers processor-based WAV/MIDI diagnostics.
