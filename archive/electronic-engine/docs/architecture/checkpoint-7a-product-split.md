# Checkpoint 7A: Vox Electronic Engine product split

Contract source: issue #11 Architecture Addenda v4.1-v4.6 and delivery policy.

## Active Vox Electronic Engine surface

- Same VST3 identity: bundle `com.ultimavox.vstengine`, manufacturer `UlVx`,
  plugin code `DkP1`.
- Bass DSP, sequencer, MIDI export, host synchronization, preset/state
  migration and common UI remain active.
- Active navigation and factory content contain Bass only until generic Rack
  lands in 7B.

## Removed product content

- Kick DSP, tests and panel moved to `legacy/vox_drum_engine`.
- MATCH analysis, tests and panel moved to `legacy/mastering_engine`.
- Neither tree has an active CMake target or runtime registration.
- CH2 and every non-Bass channel produce no audio in transitional 7A runtime.

## Compatibility boundary

- Existing Kick and MATCH APVTS IDs stay registered so Cubase automation and
  old project state remain parseable.
- Legacy Kick Part payload remains serialized and readable, but runtime always
  forces it disabled.
- Existing Kick preset files stay on disk and can be read through explicit
  migration APIs; active catalog, saving and factory loading exclude Kick.
- 7B state migration maps removed module payload to unresolved state. Silent
  fallback to Bass is forbidden.

## Automated gate

`part_audio_isolation_tests` proves Bass routing, removed-channel silence,
dormant ID round-trip and legacy Kick non-reactivation. `preset_tests` proves
active factory split, Kick creation rejection, hidden migration payload and
transactional failed loads. `editor_smoke_tests` covers Bass-only navigation.
