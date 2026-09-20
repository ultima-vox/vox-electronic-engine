# VOX Universal Engineering Agents

This directory defines a project-agnostic engineering agent system intended to work across C++, Rust, Python, TypeScript, Unreal Engine, DSP, backend, research and infrastructure repositories.

It deliberately separates **role** from **domain**.

```text
Agent Role
   +
Project Context
   +
Master Spec / Active Issue
   +
Architecture Invariants
   +
Current Repository State
   =
Project-specific execution
```

The same Implementation Agent may work on JUCE/DSP in Vox Electronic Engine, Rust/backend in Trader, UE5 in Game, or sparse neural simulation in Fly Musician. Domain behaviour comes from `PROJECT_CONTEXT.md`, not from hard-coded agent personas.

## Default topology

```text
                         ┌──────────────────────┐
                         │ Lead / Integrator    │
                         └──────────┬───────────┘
                                    │
                ┌───────────────────┼────────────────────┐
                │                   │                    │
        Implementation          Test/QA             UI/UX
                │                   │                    │
                ├──────────────┐    │     ┌──────────────┤
                │              │    │     │              │
          Reliability      Migration/Docs        Release/CI
                │              │                       │
                └──────────────┴──────────────┬────────┘
                                             │
                                  Architecture Guard
                                             │
                                           CI
                                             │
                                  Manual owner gates
                                  where actually needed
```

Fan-out is allowed only where workstreams do not edit the same contracts or depend on unfinished changes from another worker.

## Required execution cycle

1. **Context load** — read project context, current issue/spec, repository status and relevant tests.
2. **Plan** — identify touched contracts and independent workstreams.
3. **Fan-out** — delegate only independent tracks.
4. **Implementation** — smallest coherent change that satisfies the contract.
5. **Verification** — tests, static analysis, build, performance/reliability gates as applicable.
6. **Integration** — Lead resolves cross-track conflicts and validates the whole product path.
7. **Guard review** — Architecture Guard reviews any critical contract change.
8. **Manual gates** — only where automated verification cannot establish acceptance.
9. **Report** — every worker emits the common Agent Report.

## Strong defaults

- Code-first execution is preferred once the task is understood.
- Do not spend cycles repeatedly redesigning an accepted architecture.
- Do not create speculative abstractions without an active requirement.
- Do not create fake production controls, placeholder behaviour or silent fallbacks.
- Compatibility and migration are features, not cleanup work.
- Existing tests are evidence; they are not proof of untested host/user acceptance.
- Prefer deterministic tests and reproducible outputs.
- Keep domain-specific policy in the repository, not in the universal role definition.

## Repository adoption

A repository adopts this system by adding:

```text
AGENTS.md
agents/
  README.md
  AGENT_CONTRACT.md
  ROLES.md
  PROJECT_CONTEXT.md
```

Start from `PROJECT_CONTEXT.template.md`.

The shared pack can later be extracted into a dedicated `ultima-vox/engineering-agents` repository. Until then, this repository contains the canonical seed implementation.
