# Agent Development Workflow

This is the mandatory operating procedure for substantial agent-driven development in Vox Electronic Engine.

## 1. Start from current reality

Before planning or editing, the Lead must inspect:

- current `main`;
- open PRs and their base/head relationships;
- current CI/workflow state;
- issue #10 and issue #11;
- `AGENTS.md` and all files in `agents/`;
- relevant architecture/design/testing documents;
- the files and tests directly affected by the task.

Do not plan from stale chat context when repository state can be checked.

## 2. One Lead owns the task

Every substantial task has exactly one Lead / Integrator.

The Lead:

1. defines the acceptance target;
2. lists architecture-critical contracts touched;
3. decides whether fan-out is useful;
4. assigns independent tracks;
5. prevents agents from editing overlapping contracts in parallel;
6. integrates the result;
7. runs final verification;
8. produces the final AgentReport.

Workers do not independently declare the entire task complete.

## 3. Fan-out only independent work

Recommended parallel tracks for this repository:

- **Implementation** — C++/JUCE/DSP/product code;
- **UI/UX** — visual component/layout implementation against accepted design canon;
- **Test/QA** — independent tests and regression coverage;
- **Reliability/Performance** — realtime allocation, timing, lifecycle, stress;
- **Migration/Compatibility** — state/schema/identity/project restore;
- **Release/CI** — build, packaging, installer/artifact;
- **Docs/Spec** — synchronize accepted behaviour and status.

Never fan out two agents to independently redesign the same API, state schema, routing model, parameter surface or UI ownership model.

## 4. Branch and PR discipline

For each coherent task:

1. branch from current `main`;
2. use one purpose-driven branch;
3. keep unrelated refactors out;
4. open one PR against `main` unless an intentionally documented stacked PR is required;
5. if a PR becomes stacked on an obsolete/diverged branch, do not merge it merely because GitHub reports it mergeable;
6. rebuild/rebase the useful delta onto current `main` and close the obsolete PR;
7. never merge required-CI red.

Draft PRs are work in progress, not merge candidates.

## 5. Architecture gate before coding

For the proposed change, explicitly classify whether it touches:

- Rack/Router;
- SlotId / InstrumentId;
- provider/factory/descriptor contracts;
- host parameter/automation IDs;
- persistent state/schema;
- migration;
- MIDI/event ownership/timing;
- plugin/VST3 identity;
- realtime/thread model;
- resource budgets;
- package/install/update behaviour.

If yes, Architecture Guard review is required before merge.

Accepted architecture must not be reopened merely to make implementation easier.

## 6. UI work uses two gates

UI work follows the canonical design documents.

### Visual Gate A

Goal: prove composition and visual quality.

Required evidence where applicable:

- screenshots at canonical breakpoints;
- reference vs implementation comparison;
- no generic stock-JUCE appearance;
- deliberate hierarchy/density/responsive behaviour;
- explicit list of any visual-only mock data.

Visual Gate A requires explicit owner approval.

### Functional Gate B

Only after visual acceptance:

- bind all shipping controls to real descriptor/state/DSP;
- remove visual-only mock state;
- verify automation gestures/playback;
- verify preset/project restore;
- verify selected-slot refresh;
- verify routing/note ownership;
- run automated regression suite;
- perform Cubase manual checks where required.

A screenshot test is not proof of Functional Gate B.

## 7. Realtime/DSP work

For changes that can affect audio processing:

- preserve warmed `processBlock` zero-allocation guarantee;
- no locks, blocking I/O or file access in callback;
- preserve sample-accurate host event timing;
- preserve deterministic reset/offline behaviour;
- use bounded/preallocated structures;
- define deterministic overload behaviour;
- run performance and reliability gates.

If a realtime guarantee is not measured by existing tests, add instrumentation/tests rather than assuming it.

## 8. State and compatibility work

Any state/preset/schema change must include:

- schema/version impact;
- migration path;
- invalid/truncated input behaviour;
- rollback/transactional semantics;
- missing/incompatible module behaviour;
- canonical serialization impact;
- project reopen coverage.

Stable IDs must never be renumbered for convenience.

## 9. Test strategy

Tests should prove contracts, not merely execute lines.

Use as applicable:

- focused unit tests;
- integration tests;
- compliance harness;
- deterministic replay;
- state round-trip;
- migration fixtures;
- routing/note ownership cases;
- lifecycle prepare/reset/reprepare churn;
- dense performance/realtime stress;
- package/artifact validation.

Test Agent should inspect whether a test could pass while the requested behaviour is broken.

## 10. CI and artifacts

Release/CI Agent checks:

- correct Windows x64 Release configuration;
- full CTest;
- relevant stress tests;
- VST3 packaging;
- artifact contents;
- installer/package where part of the current delivery target.

Local PASS does not replace failed required CI.

## 11. Manual acceptance is explicit

The following must not be inferred from automation:

- Cubase routing behaviour;
- Cubase automation record/replay;
- project save/reopen in the host;
- owner visual approval;
- subjective sound/preset quality.

Agent reports mark these as `manual_acceptance_required` until actually performed.

## 12. Merge decision

The Lead recommends merge only when:

- PR is based on a valid current lineage;
- requested behaviour is present;
- architecture review is clear when required;
- required CI is green;
- migrations/compatibility are handled;
- no BLOCKER findings remain;
- manual gates are either passed or explicitly documented as post-merge/non-blocking by owner policy.

Do not merge a PR solely because it is technically mergeable.

## 13. Completion report

Final response must use the common `AgentReport` contract and also include:

```text
Merge recommendation: MERGE | DO NOT MERGE | WAIT
Manual gate: PASS | REQUIRED | N/A
Next task: <single concrete next action>
```

## 14. Model/tool usage

When the environment provides these capabilities:

- **Caveman**: use for concrete code-first implementation, not for bypassing architecture.
- **fan-out agents**: use only for independent tracks.
- **Ultracode/reviewer**: use as a final critical-contract review for architecture/state/routing/realtime/migration changes.
- one Lead remains accountable for integration regardless of the number of workers.

Tool availability never weakens the repository contracts above.
