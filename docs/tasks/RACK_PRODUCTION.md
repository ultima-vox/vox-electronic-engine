# Current task: complete the production Rack

Owner instruction, 26 October 2026: **Rack first; Bass afterwards.** Another agent
is developing Acid Synth and its dedicated UI. This is the current lead scope,
replacing the earlier Bass-first work allocation.

Tracking: [epic #53](https://github.com/ultima-vox/vox-electronic-engine/issues/53).
Evidence baseline: [Rack audit](../testing/rack-capability-audit-20261026.md).

## Accepted visual boundary

Owner reference `изображение(10).png` (26 October): red = shared top header and
left 16-slot Rack rail. Implement their real controls, presets, seed, telemetry,
output, panic, settings, selection, power and catalog loading. Green = Acid
workspace, including its tabs, sequencer and keyboard; do not implement or
redesign it in the Rack task. Shared backend routing remains Rack-owned.

## Ownership

| Owner | Scope |
| --- | --- |
| Rack lead | Generic libs/rack, shared libs/instrument/host automation and state, PluginProcessor, shared Rack shell/Routing UI, integration and evidence |
| External Acid agent | Acid DSP/provider parameter meanings, presets/generator and Acid-specific UI; coordinate the host binding adapter |
| Deferred | Bass DSP and full accepted VOX instrument surface after Rack milestone; further style instruments per roadmap |

No two workers independently change the shared slot/host/Pattern contracts.
The Acid agent should work against stable instrument parameter identities and a
reviewed binding interface, not hardcode an unpublished experimental macro or
host-schema mapping. The lead must publish the agreed contract before requiring
Acid UI integration. Acid implementation itself is not reassigned to the lead.

## Required work and exit evidence

### 1. Freeze the generic contracts

[#56](https://github.com/ultima-vox/vox-electronic-engine/issues/56): named direct
parameters, units/ranges/defaults/choices, controller/audio ownership, preset vs
host replay precedence, consolidated versioned state and complete resource bounds.
Review source-of-truth/rollback, duplicate/collision/overflow rejection and unknown
module state preservation. One ordinary control changes one base parameter;
explicit modulation changes effective values without cross-writing automation.

The old POC has no valuable projects requiring migration, as confirmed by the
owner. Preserve plugin identity; deliberately replace/reject obsolete POC formats
with explicit versions instead of building a parallel legacy Bass mode. Existing
main contracts remain the running baseline until the reviewed replacement merges.
Unpublished direct-control/Bass experiments are not the interface already delivered
to the Acid agent.

### 2. Audio graph and Send FX

[#54](https://github.com/ultima-vox/vox-electronic-engine/issues/54): host-owned
slot/send/return graph, stable bus identities, reviewed bus count and memory/CPU
bounds, pre/post tap points, mute/solo semantics, return effects/levels, latency,
tails and safe graph replacement. The current global post-rack EffectsChain must
not silently acquire different semantics. SemanticFx is an instrument, not a
send return. Reject unplanned feedback cycles. Test real signal isolation and
persistence before binding the Routing matrix.

### 3. Internal kick Ducking Bus

[#55](https://github.com/ultima-vox/vox-electronic-engine/issues/55): explicit
source/target mappings by stable IDs, envelope/depth/timing and retrigger policy,
reset/bypass/missing-source behaviour and gain-reduction evidence. Select/document
whether the supported source is a kick event trigger or an audio detector; do not
promise external DAW audio analysis as the same feature. Isolate kick from other
kit lanes. An explicit deterministic source fixture can validate the Rack before
Drums exists; do not start Drums DSP to hide a missing generic bus contract.

Ducking applies recipient effective gain. It must not rewrite Output Gain,
pitch, envelope or other base automation values.

### 4. Shared UI and acceptance

[#57](https://github.com/ultima-vox/vox-electronic-engine/issues/57), related #28/#5:
shared VOX Rack shell, separately identifiable MIDI/audio/ducking routing, complete
real controls, correct selected-slot mappings, unit text/reset/gestures, actual
native captures and supported scales/window sizes. Module-specific Acid UI stays
with the Acid agent. The [full Bass reference](../product/FULLON_BASS_ACCEPTANCE.md)
is retained as a later mandatory instrument scope, not implemented by a simplified
Rack controls page.

Integrated evidence must cover host input, audition, internal patterns, Generate/
Mutate publication, panic/NoteOff ownership, MIDI export/re-import, state reopen,
lifecycle/graph churn, FX tails and latency. Measure the declared active-slot /
returns / ducking workload. Scoped allocator checks do not prove a complete CPU,
leak or realtime budget. Target hardware/buffer remains an explicit open decision.

Windows x64 Release/full CTest/package and independent architecture/realtime review
precede merge. Actual Cubase12–15/REAPER workflow, visual and audio acceptance is
performed at the integrated Rack milestone, not after every PR. No standalone is
required. Do not call a working Main-only mixer a completed production Rack.

## Candidate integration status — 27 October 2026

PR #62 consolidates graph/ducking/shell and the
[named host/state contract](../architecture/RACK_PARAMETER_CONTRACT.md).
The runtime contract fixes 16 immutable slot banks, four wet returns, explicit
note-lane kick source, effective-only duck gain and zero-latency descriptor
admission. Schemas are project5/Rack4/FX2; obsolete POC state is explicitly rejected.
Missing/incompatible identities and opaque state remain recoverable.

[Evidence](../testing/rack-production-evidence-20261027.md) separates automated
checks, independent review, pre-existing Bass diagnostics and pending Windows/
manual milestone gates. Do not mark the task complete merely because the
implementation candidate exists. Acid-specific UI/DSP remains externally owned.

## Out of this task

- Bass synthesis redesign/factory content and full instrument control banks.
- Acid DSP or dedicated AcidPanel implementation owned by the other agent.
- Full Drums/Keys/Lead/FX production modules, except deterministic test providers.
- Polyphonic long-phrase Pattern v2 implementation; specify separately when its
  shared contracts are needed, without denying existing Lead/Atmos DSP polyphony.
