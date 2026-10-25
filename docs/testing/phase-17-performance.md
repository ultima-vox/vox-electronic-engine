# Phase 17 performance gate

Scope: existing synthetic regression suite; not production or DAW acceptance.
See [current status](../STATUS.md) and [product target](../product/PRODUCT_SPEC.md).

Production performance gate exercises dense rack workload through real VST3 processor shell.

## Fixed budgets

- Rack: 16 loaded/active slots maximum.
- MIDI: 2,048 routed events per slot/block; excess drops deterministically.
- Voices: 64 per slot host ceiling; each provider declares lower/equal hard ceiling.
- Modulation: 256 routes per slot host ceiling.
- Pattern: 8,192 events per slot host ceiling; encoded pattern payload 64 KiB.
- Module state: 1 MiB per slot host ceiling.
- Module resources: 64 MiB per slot host ceiling, allocated outside audio callback.
- Scratch: 8 MiB per slot host ceiling, allocated during prepare/state mutation.
- Serialized 16-slot project state: 20 MiB test ceiling.
- CPU: two simultaneous 16-slot instances must finish below 6.0 aggregate realtime ratio on Release CI hardware.
- Realtime heap: zero allocations from warmed `processBlock` under valid dense MIDI.

## Automated workload

`performance_stress_tests` loads all 16 slots in two plugin instances, distributes dense note,
CC, and note-off traffic across 16 channels. Before sustained timing, it sends exactly 2,048
simultaneous note-ons and note-offs to saturate declared voice ceilings without overflowing input.
It then runs 96 blocks at 48 kHz / 512 samples.
It rejects NaN/Inf, unexpected valid-input drops, realtime heap allocation, serialized-state
growth beyond budget, and CPU budget breach. It then performs 64 transactional unload/reload
and channel-swap cycles followed by reset/render validation.

Timing threshold guards gross regression and runaway work; it is not a product benchmark.
Publish measured ratio from hosted Release CI with Phase 17 checkpoint.
