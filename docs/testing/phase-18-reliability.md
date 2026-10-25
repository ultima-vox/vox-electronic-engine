# Phase 18 reliability gate

Scope: existing synthetic regression suite; not production or DAW acceptance.
See [current status](../STATUS.md) and [product target](../product/PRODUCT_SPEC.md).

Whole-plugin reliability coverage complements provider, Rack, preset, migration, scheduler,
and state unit tests.

`reliability_stress_tests` verifies:

- 3 sample rates × 4 block sizes × 3 host tempos with real processor lifecycle calls;
- mock host transport/PPQ/sample position through JUCE `AudioPlayHead` contract;
- finite output across 180 lifecycle/tempo renders;
- exact rollback after truncated binary state and semantically invalid Rack XML;
- v1 project migration to current schema 4 with canonical Rack and Effects state;
- 3,000 concurrent host automation changes during 256 routing/state/panic/audio cycles;
- deterministic SWAP/MOVE/LAYER conflict actions during rapid routing changes;
- repeated valid project-state restore and 32 reprepare/release cycles;
- deadlock-free completion and no NaN/Inf output.

Existing `preset_tests` remains bank-level transactional corruption/migration coverage;
`rack_integration_tests` remains Rack schema/payload corruption coverage. Phase 18 adds shell-level
composition and high-churn sequencing without weakening those focused gates.
