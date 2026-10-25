# Checkpoint 7B.0: host/module contract freeze

Contract source: issue #11 Architecture Addenda v4.1-v4.6.

## Boundary

- `InstrumentProvider` owns descriptor enumeration and off-audio-thread factory.
- `InstrumentDescriptor` carries stable `InstrumentId`, schema versions,
  parameters and mandatory resource budgets.
- `InstrumentInstance` owns prepare/reset/bypass/process, parameter updates,
  latency and tail reporting.
- `ResourceResolver` supplies already-resolved immutable bytes during module
  preparation; module processing has no filesystem API.
- `InstrumentRegistry` validates IDs, schemas, parameter manifests, duplicate
  IDs and budgets before accepting provider.
- `InstrumentAbi.h` defines versioned standard-layout C ABI types. Static
  linking uses C++ facade now; same descriptors/process model can cross DLL
  boundary later without JUCE/STL objects.

## Identity and automation

- `SlotId` is persistent 64-bit identity; rack position is not identity.
- `InstrumentId` and `ParameterId` accept stable lowercase namespaced IDs.
- Plugin exposes fixed `slot01Macro01` through `slot16Macro08` APVTS bank.
- Labels are fixed Cutoff, Resonance, Envelope, Decay, Drive, Accent, Space,
  Motion. Module changes remap values; they never replace host parameter IDs.
- Existing APVTS IDs remain untouched and precede new bank.

## State and failure policy

- `PersistentModuleState` and `RuntimeModuleState` are separate types.
- State preparation builds replacement instance before live-state swap.
- Missing/incompatible/over-budget/construction failures remain explicit
  `ResolutionStatus`; payload remains intact. No fallback instrument.
- Newer unsupported state schema and over-budget payload fail before module
  construction.

## Realtime contract

- Provider enumeration, module construction, resource resolution, state load
  and preparation are non-realtime operations.
- `reset`, `setBypassed`, `setParameter`, latency/tail queries and `process` are
  `noexcept` where invoked by realtime host.
- Process receives bounded spans over host-owned audio/MIDI memory. Contract
  exposes no lock, allocator, filesystem or construction service.

`instrument_contract_tests` covers ABI layout, 128 stable macro IDs, provider
validation, missing/incompatible/budget states, lifecycle matrix at
44.1/48/96 kHz and block sizes 1/64/511/2048, explicit bypass, finite render,
single construction and unresolved payload preservation.
