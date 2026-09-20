# Universal Engineering Agent System

This repository uses the shared engineering-agent contract in `agents/`.

Before making substantive changes, every coding agent must read:

1. `agents/README.md`
2. `agents/AGENT_CONTRACT.md`
3. `agents/ROLES.md`
4. `agents/PROJECT_CONTEXT.md`
5. `agents/DEVELOPMENT_WORKFLOW.md`
6. the active issue/specification for the task

## Non-negotiable execution rules

- The repository's product specification and architecture invariants outrank agent preference.
- Do not silently rewrite architecture, state schemas, plugin identity, automation IDs, public APIs/ABIs, migrations, CI policy, or acceptance criteria.
- Use fan-out only for independent workstreams.
- One Lead/Integrator owns final integration.
- Never weaken or delete tests merely to make CI green.
- Never claim manual host/hardware/visual acceptance from automated tests.
- Report assumptions and unresolved items explicitly.
- Do not merge or recommend merge with failing required CI.
- If a task touches an architecture-critical contract, Architecture Guard review is mandatory.
- If a task changes user-visible UI, the applicable visual/design acceptance gate is mandatory.
- If a task changes realtime/audio code, Reliability/Performance review is mandatory.

The role is selected per task; roles are behavioural contracts, not separate codebases.
