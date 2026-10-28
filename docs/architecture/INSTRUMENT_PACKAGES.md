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
N = 64 (owner decision): 16 × 64 = 1024 pool parameters plus the macros.

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

Layout: one folder per package, `<id>.voxinst/` (owner decision: folders):

```
manifest.json   id, name, vendor, version, engineApi, schema
graph.json      blocks, connections, voice policy
params.json     package parameter -> explicit pool index (stable across package
                versions; never reused for another parameter), range, unit, default
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

The instrument page renders `ui.json` with VOX widgets. Owner decision:
built-in blocks (Bass, Acid) may provide a native panel. The Bass UI follows
the same instrument-page grammar as every other instrument (header, tabs,
panels, knobs); it is not a separate shell.

## Phases

1. Parameter pool replaces `DirectParameterBank` IDs (**done** on
   `feat/instrument-param-pool`, see [parameter contract](RACK_PARAMETER_CONTRACT.md));
   built-in instruments become manifest + coarse block (next). Plugin parameter
   layout changes once.
2. Package loader/scanner, validation, missing-package handling, tests.
3. Block graph compiler and the first fine-grained blocks.
4. `ui.json` renderer; packaging tool.

## Open questions for the owner

1. Install root per OS (proposal: Windows `%PROGRAMDATA%\Ultima Vox\Instruments`
   plus a user folder; macOS `/Library/Application Support/Ultima Vox/Instruments`
   plus `~/Library/...`).
2. Licensing/encryption of packages (out of scope until asked).

Resolved 27.10.2026: 64 pool parameters per slot; folder packages; native
panels allowed for built-in blocks, Bass UI uses the common instrument grammar.
