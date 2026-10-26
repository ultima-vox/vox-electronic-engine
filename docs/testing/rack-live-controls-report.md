# Rack live-control synchronization (#47)

## Defect and scope

`setMacro` called the live provider directly with a normalized 0..1 value even
though `InstrumentInstance::setParameter` accepts plain units. It could also
enter DSP concurrently with `process`. `updateControls` changed persistent
values while the default audio-block controls were copied without exclusion.
`runtimeState` copied metadata while control-side replacement could write it.
These are source-confirmed defects, not established causes of the reported Bass sound.

## Publication and compatibility

- Direct setters are non-realtime APIs: persistent mutex serializes writers;
  the existing mutation gate waits for active processing before changing defaults.
- `setMacro` never calls DSP. Next audio block uses existing normalized-to-plain
  conversion, including parameter range/step. Normalized values persist immediately.
- Explicit `ProcessSlotControls` override persistent defaults. APVTS automation
  continues through that block snapshot path and does not use the direct setters.
- A callback overlapping a direct mutation can skip contributing audio, as with
  other existing Rack gated mutations. These APIs are not the automation hot path.
- `runtimeState` is a non-realtime mutex-protected metadata snapshot; dropped-event
  counters remain atomic. `ownedNotes` is reserved/always zero, not an active meter;
  reset no longer redundantly writes it from the audio path.
- No external ABI, schema, parameter/slot/module/plugin IDs change. No migration.
  No callback lock, allocation, or retry loop was added.

Borrowed `state()`/`descriptor()` views still require caller serialization against
control mutations; lifecycle latency/tail queries also require serialization.
This patch does not certify the complete processor/editor as concurrently safe.
Existing shell calls to `reset` outside Rack processing are a separate audit area.
Only one audio consumer per Rack is supported by this workload.

## Verification

Baseline test failed before the fix: `setMacro must not call DSP on control thread`.
New `rack_live_controls_tests` exercises actual Rack with a test-only provider:
plain range -20..20 with step 5, immediate persistence, next-block sound output,
clamps/nonfinite/invalid indices, explicit/default precedence, mute/pan/level,
2,000 direct macro/control updates overlapping audio, 100 module replacements
while runtime metadata is polled, and subsequent usability.

Local Linux GCC 13.3, actual pinned JUCE 9.0.1 core/events/data_structures:
- New suite: 16 checks PASS normally and with project AddressSanitizer/UBSan.
- LeakSanitizer disabled because of this environment's process-inspection limits;
  precompiled JUCE objects are not sanitizer-instrumented. No leak/TSan claim.
- Integrated lifecycle/router regression results are recorded with the PR.
- Windows x64 full build/CTest/package remains the required CI gate.
- Cubase/REAPER and sound acceptance: REQUIRED AT MILESTONE, not run locally.

## Independent review

Architecture Guard/realtime review: no BLOCKER or MAJOR; reviewer independently
ran the 16-check suite. No claim that churn tests prove absence of all races.

## AgentReport

```yaml
agent_report:
  role: Lead / Integrator
  task: Synchronize direct rack controls and correct plain macro application
  result: PASS
  files_changed:
    - libs/rack/Rack.cpp
    - libs/rack/Rack.h
    - tests/rack/RackLiveControlsTests.cpp
    - CMakeLists.txt
    - agents/DEVELOPMENT_WORKFLOW.md
    - docs/BUILD.md
    - docs/STATUS.md
    - docs/README.md
    - docs/product/PRODUCT_SPEC.md
    - docs/testing/rack-live-controls-report.md
  contracts_touched: [thread ownership and control publication]
  assumptions: [one audio consumer, direct setters are non-realtime]
  implementation: [gated persistent defaults, audio-owned DSP mapping, metadata snapshot lock]
  tests_added: [rack_live_controls_tests]
  verification:
    - command: build/rack-audit/rack_live_controls_tests
      result: PASS
      notes: 16 checks; baseline failed before fix
    - command: ASAN_OPTIONS=detect_leaks=0 build/rack-audit/rack_live_controls_tests_sanitized
      result: PASS
      notes: 16 checks; project source instrumentation
  architecture_changes: [existing mutation gate reused]
  migration_impact: [none]
  performance_impact: [direct mutations may skip overlapping callback; hot automation unchanged]
  security_impact: [none]
  manual_acceptance_required: [integrated Windows DAW and sound milestone]
  unresolved: [borrowed-view and external reset caller discipline]
  risks: [no complete shell concurrency or sound-quality certification]
```
