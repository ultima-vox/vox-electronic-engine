# Instrument packages (proposal, needs Architecture Guard review)

Status: **PROPOSED**, 27.10.2026. No code yet. Supersedes the "native module
loader on C ABI v1" idea recorded earlier the same day.

## Owner decisions (27.10.2026)

1. Kontakt model: one Rack plugin, many instruments installed separately.
2. Instruments are **data only**: no native code inside a package. A new DSP
   algorithm ships with an engine (plugin) update; a new instrument does not.
3. One instrument = one MIDI channel (implemented on `claude/clever-cori-9bsvqy`,
   see [CONTRACTS](CONTRACTS.md)). Key/velocity zones and transpose stay.
4. No legacy support: old POC projects and layered states are rejected.
5. A fixed host-parameter pool per slot replaces per-instrument direct IDs.

## Why the parameter pool is required

Today `DirectParameterBank` creates `slotNN.sound.<InstrumentId>.<ParameterId>`
for every compiled instrument × 16 slots when the processor is constructed. A
package installed later would change the VST3 parameter list, which hosts do
not support reliably. The pool keeps the list fixed:

| ID | Meaning |
| --- | --- |
| `slotNN.pKKK` (KKK = 001..N) | Instrument parameter K of the slot's package, mapped by the package manifest |
| `slotNNMacroMM` | Existing 8 macros, unchanged |

Names follow the loaded package (`updateHostDisplay` with parameter-info
change). Automation written for one instrument plays into whatever instrument
occupies the slot later; this is the Kontakt behaviour and must be documented.
N is an open question (proposal: 64; 16 × 64 = 1024 host parameters).

## Engine and package boundary

- The engine ships a library of realtime-safe **blocks**: oscillators,
  filters, envelopes, LFOs, waveshapers, sample player, FX. Built-in voices
  (Psy Bass, Acid, Lead, Atmos, Semantic FX) become coarse blocks so they can be
  packaged without rewriting their DSP. Acid DSP stays owned by its agent;
  wrapping it as a block needs that agent's agreement.
- A package describes a block graph, parameter mapping to the pool, presets,
  pattern/generator grammar and a UI layout. The graph is validated and compiled
  into a fixed processing plan off the audio thread; the audio thread only swaps
  a prepared plan (same transactional parse → validate → prepare → commit).
- Budgets reuse `HostBudgetLimits` (voices, state, resources, scratch).

Proposed layout (`<id>.voxinst`, folder or zip):

```
manifest.json   id, name, vendor, version, engineApi, schema
graph.json      blocks, connections, voice policy
params.json     package parameter -> pool index, range, unit, default
presets/        sound presets
patterns/       pattern presets and generator profiles
ui.json         layout of standard VOX widgets (no custom code)
assets/         samples, thumbnail
```

## Lifecycle

- Install location per OS (open question); scanned on the message thread at
  processor construction and on explicit rescan, never in the audio callback.
- A project referencing a missing package keeps the slot, identity and payload
  (existing missing-module policy) and plays silence.
- Package `engineApi` newer than the engine → rejected with a clear message.

## UI

The instrument page renders `ui.json` with VOX widgets. Open conflict: the
mandatory full VOX Bass interface and the dedicated Acid UI may need richer
widgets than a generic layout; decide whether built-in blocks may provide a
native panel.

## Phases

1. Parameter pool replaces `DirectParameterBank` IDs; built-in instruments
   become manifest + coarse block. Plugin parameter layout changes once.
2. Package loader/scanner, validation, missing-package handling, tests.
3. Block graph compiler and the first fine-grained blocks.
4. `ui.json` renderer; packaging tool.

## Open questions for the owner

1. Pool size per slot (proposal 64).
2. Install folder and whether packages are zip or folders.
3. Licensing/encryption of packages (out of scope until asked).
4. Native panels for built-in blocks (Bass/Acid) or `ui.json` only.
