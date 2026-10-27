# Protected contracts and intentional evolution

Product authority: [PRODUCT_SPEC](../product/PRODUCT_SPEC.md).
These safety/compatibility protections remain in force with the owner-authorized
POC replacement policy below.

## Current scope and deliberate POC replacement

Rack completion is the current priority; Bass follows. Acid DSP and dedicated UI
are separately owned. One lead coordinates shared Rack/host/state interfaces;
unmerged experiments are not a frozen contract. See [current task](../tasks/RACK_PRODUCTION.md).

The owner confirms no old POC projects need migration. Explicit versioned
replacement/rejection of obsolete POC parameter/state meanings is authorized;
do not add parallel legacy Bass modes or silently reinterpret payloads. This does
not authorize plugin identity changes, hidden parameter coupling, unsafe realtime
work or loss of MIDI ownership. One ordinary control changes one base parameter;
explicit modulation must not rewrite other host base values.

## Preserve now

- One generic VST3 host with at most 16 rack slots.
- Provider/descriptor/factory boundary; no concrete instrument routing in the shell.
- Stable SlotId independent of order, MIDI channel, InstrumentId and audio output.
- NoteOff returns to the exact slot that accepted NoteOn.
- One instrument = one MIDI channel (owner decision 27.10.2026). A channel has at
  most one owning slot; a loaded instrument takes the next free channel; there is
  no Layer mode and no SWAP/MOVE. Key/velocity zones and transpose filter the
  slot's own channel. Rack schema 5 rejects layered or duplicate-channel states.
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
| Send FX / return graph | Stable audio bus identities, tap points, mute/solo/tail/latency policy, state/automation, feedback rejection and bounded buffers; candidate PR #62 |
| Internal kick ducking | Explicit source/recipient contract and event-vs-audio-detector semantics, envelope policy and effective-gain routing; candidate PR #62 |
| Complete direct parameter binding | Named host identities, source-of-truth/rollback, full bank/resource bounds and consolidated reviewed state changes |
| Modular paid distribution | Offline content/entitlement policy; no hidden network dependency |

The Rack candidate deliberately evolves project/Rack/FX schemas to 5/4/2 and
adds the [named parameter/state contract](RACK_PARAMETER_CONTRACT.md) and
[audio routing contract](RACK_AUDIO_ROUTING.md). It remains subject to independent
review, required CI and the integrated host milestone; implementation presence
alone does not establish production acceptance.
For each future critical change record old/new versions, migration/failure behaviour,
tests and Architecture Guard review before merge.

## Historical code

`libs/parts/`, legacy parameter IDs and existing modules are still active build/compatibility inputs.
They are not archived without an audit of CMake, includes, state/preset loaders and tests.
The historical inactive source is under [archive](../../archive/README.md).
Do not reactivate it merely because it has been preserved.
