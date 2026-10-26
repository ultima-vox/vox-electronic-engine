# Protected contracts and intentional evolution

Product authority: [PRODUCT_SPEC](../product/PRODUCT_SPEC.md).
These existing safety/compatibility protections remain in force except for the
owner-authorized POC Bass replacement below.

## Owner-authorized POC reset, 26 October 2026

The owner confirms there are no old projects and rejects keeping legacy tails.
Replace coupled POC Bass sound/automation/state with independent Full-on Bass,
without a parallel compatibility mode. This overrides preservation of that POC
mapping, not realtime safety, routing ownership, plugin identity or validation.
Version deliberate state/automation breaks and reject unsupported old data
explicitly. See [independent parameters](independent-sound-parameters.md).

## Preserve now

- One generic VST3 host with at most 16 rack slots.
- Provider/descriptor/factory boundary; no concrete instrument routing in the shell.
- Stable SlotId independent of order, MIDI channel, InstrumentId and audio output.
- NoteOff returns to the exact slot that accepted NoteOn.
- Explicit SWAP/MOVE/LAYER semantics; no accidental duplicate-channel layering.
- Sound and Pattern remain independent.
- Transactional parse -> validate -> prepare -> commit state loads.
- Missing/incompatible modules preserve identity and payload; no silent substitution.
- No blocking locks, file/network I/O, construction or unbounded allocation in audio callbacks.
- Preserve existing tests, realtime bounds, lifecycle safety and determinism policy.
- Existing plugin, parameter, instrument and schema identifiers are not renamed for branding.

Current plugin identity: bundle `com.ultimavox.vstengine`, manufacturer `UlVx`,
plugin code `DkP1`. Current product/package name: Vox Electronic Engine.
The new product name alone does not authorise an identity or install-path change.

## Agreed product changes requiring implementation review

| Change | Required implementation work |
| --- | --- |
| Full-on Bass/Drums/etc as separate rack instruments | Stable new IDs, provider descriptors, catalog/state tests; no reuse of legacy IDs |
| Drums inside Trance Engine | Kit/pad ownership, choke/resource/event budgets; archived Kick is a candidate, not an accepted kit |
| Keys and long polyphonic phrases | Pattern v2 contract, adapter/migration, scheduling and export tests |
| Additional slot audio outputs | Stable bus topology, project migration and actual host testing |
| Per-slot effects | Explicit routing/state/resource design; current global chain is not silently reinterpreted |
| Modular paid distribution | Offline content/entitlement policy; no hidden network dependency |

No implementation in this cleanup changes those runtime contracts.
For each future critical change record old/new versions, migration/failure behaviour,
tests and Architecture Guard review before merge.

## Historical code

`libs/parts/`, legacy parameter IDs and existing modules are still active build/compatibility inputs.
They are not archived without an audit of CMake, includes, state/preset loaders and tests.
The historical inactive source is under [archive](../../archive/README.md).
Do not reactivate it merely because it has been preserved.
