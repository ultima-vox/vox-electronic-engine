# Vox Trance Engine — engineering entry point

Before substantive work read:
1. [Documentation authority](docs/README.md).
2. [Product specification](docs/product/PRODUCT_SPEC.md).
3. [Current status](docs/STATUS.md) and [roadmap](docs/ROADMAP.md).
4. [Protected contracts](docs/architecture/CONTRACTS.md).
5. [Agent contract](agents/AGENT_CONTRACT.md), [roles](agents/ROLES.md),
   [project context](agents/PROJECT_CONTEXT.md) and [workflow](agents/DEVELOPMENT_WORKFLOW.md).
6. The active scoped task; for AGY: [Bass diagnostics](docs/tasks/AGY_BASS_DIAGNOSTICS.md).

Current owner instructions and the current product specification supersede the old
Electronic Engine scope in issues #10/#11. Their snapshots are historical.
Do not use archived documents as active instructions.

## Non-negotiable rules

- Preserve plugin identity, parameter/InstrumentId/SlotId identity and compatible project state.
- Do not silently change ABI, schemas, routing, automation or acceptance criteria.
- Critical contract changes require Architecture Guard review and migration/test evidence.
- Audio work requires realtime/performance review; no blocking I/O, locks or unbounded callback allocation.
- Never weaken/delete regression tests to make CI green; never recommend merge with required checks failing.
- UI work uses the current design/workflow docs and real-editor visual and functional gates.
- Do not create drawings/mockups/artwork until the owner explicitly requests them.
- Automated tests do not establish DAW routing, sound quality or visual acceptance.
- Use independent agents only on independent scopes; one Lead integrates and verifies.
- Work on a purpose-driven branch; do not merge/force-push another worker's branch.
- Archive code only after checking CMake, includes, runtime registration, state migration and tests.
- Do not reimplement the rack or generate a new host merely to diagnose Bass.
