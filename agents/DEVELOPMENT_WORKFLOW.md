# Development workflow

## 1. Load current reality

Read [AGENTS](../AGENTS.md), [documentation authority](../docs/README.md),
the product/status/roadmap/contracts, current main and relevant open PRs/checks.
Read the active task and actual affected source/tests.
Issues #10/#11 are historical product/roadmap records, not current authority.

## 2. Scope and ownership

Current owner allocation: Rack lead owns generic Rack/host/state/shared UI; another
agent owns Acid DSP and its dedicated UI; Bass follows the Rack milestone.
Read [current task](../docs/tasks/RACK_PRODUCTION.md).

One Lead owns integration. Record objective, acceptance, files, assumptions and required evidence.
Use a separate branch for one coherent task.
Independent workers may operate in parallel only when they do not redefine shared contracts.
Do not revert another worker's changes, silently merge their branch or overlap file ownership.
Reuse accepted code; remove only what is proven inactive or has an explicit migration/removal plan.

## 3. Critical-contract gate

Before changing Rack/Router, identities, provider/factory/ABI, host automation,
state/schema/migrations, event timing/ownership, realtime/resource policy or packaging:
document the compatibility impact and obtain Architecture Guard review before merge.
Accepted product evolution does not authorise an unreviewed runtime-contract rewrite.
Use an independent review pass for these changes when agents are available.
Skill/tool workflows may support the work but never replace tests or manual host evidence.

## 4. Implement and prove

- Keep deterministic generation, explicit seed/version/state semantics and bounded resources.
- No blocking locks, disk/network I/O or unbounded audio-thread allocation.
- Add regression tests for demonstrated defects; never delete/weaken tests to pass CI.
- State loads are transactional; invalid or missing content does not silently substitute another sound.
- Preserve lifecycle reset/reprepare/suspend and note-release safety.
- Keep implementation and unrelated architecture/UI redesign in separate tasks.

## 5. UI and manual gates

Read current UI docs before user-visible changes.
Visual Gate A: actual editor screenshots/comparison, scaling checks and explicit owner acceptance.
Functional Gate B: real DSP/state bindings, gestures/automation, presets, routing and regression checks.
Do not introduce visual-only controls into production.
Sound quality, DAW routing/automation, drag-and-drop, project reopen and offline render require actual host evidence.
Owner policy (26.10.2026): manual DAW checks occur at integrated milestones
(Rack, complete Full-on Bass, release candidate), not after every PR.
Mark unperformed manual checks REQUIRED AT MILESTONE; never infer them from CTest.
A technical PR may merge after required automated checks and independent review
without waiting for that milestone. VST3 is the product; standalone is not required.
New drawing/artwork/mockups require an explicit owner request.

## 6. Verify and release

Use [build commands](../docs/BUILD.md).
Functional PRs require Windows x64 Release, full CTest and relevant stress/package checks.
No required-CI-red merge. Do not use green build status as proof of production acceptance.
Docs-only changes require link/reference/archive-integrity/diff validation;
their new CI result must still be reported accurately.
Archive moves must preserve bytes and leave active build/runtime/migration paths intact.

## 7. Report and integrate

Return the [AgentReport](AGENT_CONTRACT.md) with concrete evidence and unresolved items.
State MERGE / DO NOT MERGE / WAIT, manual gate PASS / REQUIRED / N/A, and the next concrete task.
A draft PR is reviewable work, not a completed product.
Do not claim a feature, test or host result that was not observed.
