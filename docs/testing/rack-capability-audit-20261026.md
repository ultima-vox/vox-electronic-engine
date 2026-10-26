# Rack capability audit — 26 October 2026

Audited remote main: `ccc1b4dbe416d96370721674eb4857aec19f8fac`.
The clean local review checkout `cb7e45f3c8f5c3b81290e9f61d024d6d86e6e8e2`
has the identical source tree `6e4fd4df5232b12c41bb2d436761aa4f375e3f4c`.
Unmerged Bass/direct-parameter/UI experiments were excluded from main findings.

Owner priority: finish the generic Rack first, Bass afterwards. Another agent
owns Acid Synth and its dedicated UI. Active scope: [Rack task](../tasks/RACK_PRODUCTION.md),
[epic #53](https://github.com/ultima-vox/vox-electronic-engine/issues/53).

## Findings

| External claim / question | Verified state in main | Source |
| --- | --- | --- |
| Internal kick Ducking Bus exists | **Missing.** No detector/trigger bus, ducking envelope or recipient gain routing in the audited path. Distinguish future internal event-triggered ducking from audio detection/external DAW sidechain. | [ProcessBlock](../../libs/instrument/InstrumentContract.h), [processor](../../apps/vst3/PluginProcessor.cpp), [Rack](../../libs/rack/Rack.cpp) |
| Send FX / return matrix exists | **Missing.** Per-slot scratch audio is mixed by level/pan into Main; only outputDestination=0 is accepted. No send levels/returns. | [Rack.cpp](../../libs/rack/Rack.cpp), [RackTypes](../../libs/rack/RackTypes.h), [state validation](../../libs/rack/RackState.cpp) |
| RackRouter routes audio buses | **Incorrect.** It routes MIDI/ownership by SlotId/channel/note. A generic audio bus graph remains to be implemented; it must not be confused with MIDI routing. | [RackRouter](../../libs/rack/RackRouter.h) |
| No effects exist | **Incorrect.** One global eight-stage post-rack EffectsChain exists: distortion, wavefolder, phaser, flanger, chorus, bitcrusher, delay, reverb. It is not a send matrix or per-slot insert bank. | [EffectType/chain](../../libs/effects/EffectsChain.h), processor Rack call followed by EffectsChain call |
| Routing tab is a send/sidechain matrix | **Incorrect.** Current controls are MIDI assignment/SWAP/MOVE/LAYER, enabled/mute/solo/lock, level/pan, with actual APVTS bindings. | [RoutingPage](../../apps/vst3/PluginEditor.cpp) |
| Macros must be connected to SemanticFx | **Misleading.** MacroPage already binds selected-slot macro endpoints. SemanticFx is a MIDI instrument producing FX sounds, not a return-bus processor. | [MacroPage::bind](../../apps/vst3/PluginEditor.cpp), [SemanticFxProvider](../../libs/semanticfx/SemanticFxProvider.cpp) |
| Modulation does not exist | **Incorrect.** Backend configure/restore/evaluate exists. A complete user matrix/source-bank editor does not. Current helper has two fixed-rate LFOs and block-derived envelope/STEP/random, not the full accepted synth source banks. | [Rack::configureModulation/applyModulation](../../libs/rack/Rack.cpp), [Modulation](../../libs/modulation/Modulation.h) |
| BassEngine is an existing class in libs/bass | **Not found in main.** Active built-in BassInstance wraps PsyBassVoice directly. libs/bass contains PsyBassVoice and BassGenerator. | [BuiltInProvider](../../libs/modules/BuiltInProvider.cpp) |
| STATUS proves PsyBassVoice NoteOff/Release is broken | **Not established.** STATUS explicitly records an owner symptom with unknown cause. The active adapter calls noteOffDirect(true); voice calls ADSR.noteOff and stops after inactivity. A fresh direct-voice probe passed, as below. This does not reproduce the owner's complete old session. | [Bass adapter](../../libs/modules/BuiltInProvider.cpp), [voice](../../libs/bass/PsyBassVoice.cpp), [STATUS](../STATUS.md) |
| Generation path is currently severed | **Not established as a general claim.** Provider generation -> conversion/encoding -> slot pattern commit -> bridge publication -> audio bridge read -> per-slot scheduler -> routed MIDI -> Rack is implemented. Transport/source mode/locks also gate playback; Generate is not an unconditional host-play command. | PluginProcessor: prepareGeneratedPattern/generateSelectedPattern, publishPartSequenceForAudio, processBlock |
| Scheduler/export defects were never fixed | **Outdated.** Merged PR #51 repairs onset-aware PPQ releases, carried ratchets and active-slot MIDI export, with actual SMF replay diagnostics. Its head cb7e45f Windows CI succeeded. Exact old-session causality and musical/DAW acceptance remain unconfirmed. | [integration report](bass-diagnostics-integration-report.md), [PR #51](https://github.com/ultima-vox/vox-electronic-engine/pull/51), [CI](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/40275400131) |
| Lead/Atmos have no polyphony | **Incorrect for DSP.** LeadEngine has 8 voices; AtmosEngine has 6. The missing polyphonic/long-phrase pattern and scheduler model is a separate issue. | [LeadEngine](../../libs/lead/LeadEngine.h), [AtmosEngine](../../libs/atmos/AtmosEngine.h) |
| Current sequence supports arbitrary chords/long phrases | **No.** Sequence/Step and VoxPatternV1 have one noteOffset per step; GeneratedNoteScheduler tracks one held generated note. Pattern v2 is still planned. Do not call DSP voice polyphony a completed polyphonic sequencer. | [Sequence](../../libs/sequence/Sequence.h), [scheduler](../../libs/midi/GeneratedNoteScheduler.h), [ABI pattern](../../libs/instrument/InstrumentAbi.h) |
| Active UI is just BassPanel | **Incorrect.** BassPanel exists/builds, but the active rack editor uses generic Sound/Macros pages and six page types. Dedicated AcidPanel/LeadPanel/AtmosPanel are absent in audited main; Acid-specific UI is now separately owned. | [PluginEditor.h](../../apps/vst3/PluginEditor.h), [PluginEditor.cpp](../../apps/vst3/PluginEditor.cpp), [navigation](../../libs/ui/MainNavigation.cpp) |
| Independent Note/Octave lanes already exist | **Partial.** There is a Note lane editing noteOffset -24..24 and octave shift actions, not the proposed separate pitch-class and per-step octave lanes/global-scale workflow. | [StepSequencer](../../libs/ui/StepSequencer.cpp) |
| Drums/Keys/Trance Gate are complete | **Missing as complete active modules/workflows.** No active Drum Kit/Keys providers or dedicated Trance Gate implementation was found. Archived Kick is not a finished kit. | [built-in registration](../../libs/modules/BuiltInProvider.cpp), [roadmap](../ROADMAP.md) |

The current Acid filter is four nonlinear one-pole stages with ladder-style
feedback. The code alone does not establish the proposed selectable diode18/
LP24 topologies or all new Acid UI controls. Their implementation belongs to the
Acid agent; this audit does not redesign that module.

## Focused release verification

A temporary test compiled the **unchanged main** PsyBassVoice.cpp against real
pinned JUCE9.0.1 code. Rates44.1/48/96kHz × blocks64/512/1024 tested audible
NoteOn, NoteOff with35ms release reaching exact zero within200ms, and forced
reset producing zero audio. **27 checks PASS** with project ASan/UBSan;
LeakSanitizer disabled. Real JUCE audio/core dependency objects were precompiled,
not fully sanitizer-instrumented. A real JUCE Colour translation unit satisfied
main header dependencies; no mock voice/envelope implementation was substituted.

This verifies one direct voice lifecycle path. It does not prove the complete
host/source/state/routing session cannot lose an event, nor establish audible
Full-on quality. No DAW, installer or new full Windows build was run for this
read-only audit. The historical successful Windows CI above is separately scoped.

## Decisions and tracked work

- [#53](https://github.com/ultima-vox/vox-electronic-engine/issues/53): production Rack milestone and ownership.
- [#54](https://github.com/ultima-vox/vox-electronic-engine/issues/54): audio graph / Send FX / returns.
- [#55](https://github.com/ultima-vox/vox-electronic-engine/issues/55): internal kick ducking.
- [#56](https://github.com/ultima-vox/vox-electronic-engine/issues/56): generic direct parameters / automation / state.
- [#57](https://github.com/ultima-vox/vox-electronic-engine/issues/57): shared Routing UI / integrated acceptance.
- [#58](https://github.com/ultima-vox/vox-electronic-engine/issues/58): full accepted Bass, deferred until Rack.
- Existing #45 symptom acceptance remains open/deferred; #4 performance, #5 MIDI
  and #28 shared UI are updated to the new priority. No guessed root cause is
  marked repaired and no known acceptance gap is closed by this audit.

## Review outcome

Independent Architecture Guard inspected the same clean tree for routing/effects
and confirmed the missing buses and real current bindings. **DO NOT call the Rack
production-complete.** Next runtime work is a reviewed generic bus/control/state
contract, followed by implementation and Rack milestone evidence. Bass DSP and
Acid-specific UI are outside the current lead's implementation scope.

Main rechecked after audit: `7d4f7ac1c4e7a558925471359cceda4819a705ce` restores the same audited tree `6e4fd4df5232b12c41bb2d436761aa4f375e3f4c`. The external Acid candidate is preserved separately in `backup/gemini-acid-2026-10-26`; this audit does not claim its integration.
