# Per-slot runtime diagnostics

Status: hardening backlog P1 item 1 (#78, owner comment 6508394151).
- **DONE (this slice):** the backend.
- **NOT YET:** processing-time profiling (item 2) and the diagnostics view (item 3).

## API

`Rack::diagnostics()` runs on the controller thread. It takes the Rack's
control-state mutex, the same one every instrument swap holds, and returns one
`SlotDiagnostics` per slot:

| Field | Source |
| --- | --- |
| `slotId`, `instrumentId`, `name`, `publisherId`, `instrumentVersion` | slot state / descriptor |
| `resolution` | existing `RuntimeSlotState` resolution status |
| `active` | a playable instance is installed |
| `bypassed` | disabled, muted or solo-excluded in the last processed block |
| `droppedMidiEvents` | existing last-block router drop count |
| `droppedMidiEventsTotal` | router drops since the instrument was built |
| `noteOutputOverflowBlocks` | blocks whose generated notes exceeded the host buffer (module-reported `dropped`) |
| `health.midiOverflowBlocks` | blocks with more routed MIDI than the module's prepared budget |
| `health.invalidNoteBlocks` | blocks whose note output was voided as malformed |
| `health.faulted`, `health.faults`, `health.lastFault` | latched native fault, count, last reason (`processFailed` / `resetFailed`); the reason survives a recovering reprepare |
| `lastError` | why the last load/restore of the slot failed, or why an accepted slot stays unresolved (missing module, other publisher, newer pattern schema); empty when resolved |

Package-level errors are not per-slot data. Untrusted signatures, hash
mismatches and conflicts stay in the loader's report
(`nativeModuleDiagnostics()`). A project slot that referenced such a package is
unresolved, and its `lastError` says so.

## Realtime rules

The audio thread only does relaxed `fetch_add` / `store` on fixed per-slot and
per-instance atomics:
- no allocation, lock, string, log, file access or UI work;
- `native_rack_tests` `noAllocationInProcess` stays at 0 allocations.

The controller reads instance health through `InstrumentInstance::health()`,
which reads atomics only. The default implementation reports nothing.

Counters restart when the slot's instrument is rebuilt (load, project
restore). A reprepare keeps them.

## Tests

`native_rack_tests` `slotDiagnostics` covers:
- identity and zero counters after normal playback with generated notes;
- an empty slot;
- module MIDI overflow on its own slot only;
- note-output overflow vs malformed notes;
- bypass by mute and by solo elsewhere;
- process fault, its reason, and recovery;
- an unknown instrument (unresolved, with a reason) and a reload that clears it;
- a missing module after a project restore.

Mutation checks: removing the overflow counter or the bypass store fails the test.
