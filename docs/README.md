# Documentation

Read [STATUS](STATUS.md) to distinguish existing code from production targets.

## Authority

1. Current explicit owner instructions.
2. [Product specification](product/PRODUCT_SPEC.md): agreed product direction and labelled open implementation decisions.
3. [Protected contracts](architecture/CONTRACTS.md): existing identity, routing, realtime and migration protections.
4. [Roadmap](ROADMAP.md) and the current scoped task.
5. [Design system](ui/DESIGN_SYSTEM.md) and [UI workflows](ui/WORKFLOWS.md).
6. [Agent contract](../agents/AGENT_CONTRACT.md) and [development workflow](../agents/DEVELOPMENT_WORKFLOW.md).

A product target is not permission to silently change the existing ABI/state schema.
Implementing a contract change requires its own migration analysis, tests and review.

## Working documents

| Document | Purpose |
| --- | --- |
| [Product](product/PRODUCT_SPEC.md) | Instrument model, offline generation, exports, scope and open decisions |
| [Status](STATUS.md) | Observed implementation, owner-reported defects, unverified acceptance |
| [Roadmap](ROADMAP.md) | Ordered work and deliverables |
| [Contracts](architecture/CONTRACTS.md) | Boundaries that implementation must preserve |
| [Build](BUILD.md) | Real commands, artifact names and check limitations |
| [Design system](ui/DESIGN_SYSTEM.md) | Current component/token locations and interaction rules |
| [UI workflows](ui/WORKFLOWS.md) | Rack, instrument pages, generators and visual gates |
| [AGY Bass task](tasks/AGY_BASS_DIAGNOSTICS.md) | Independent, bounded development packet |
| [Performance suite](testing/phase-17-performance.md) | Existing synthetic workload and limits |
| [Reliability suite](testing/phase-18-reliability.md) | Existing automated lifecycle/state workload |

## Historical material

[Archive index](../archive/README.md) records moved files and exact-content hashes.
Legacy issues #10/#11 and historical UI documents do not override the current product model.
They remain useful for implementation provenance and migration research.
