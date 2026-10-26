# Independent Sound parameters

Owner requirement, 26 October 2026: one ordinary control changes one named
parameter. Linked changes require an explicitly labelled Macro or visible,
disableable modulation route. The native Sound UI is not complete until this
holds in DSP, automation and restored state.

## Intentional replacement of the POC

The owner explicitly confirms there are no old projects and rejects retaining
legacy compatibility paths. The POC is not a production release. This supersedes
the earlier proposal to preserve the coupled Psy Bass controls.

Replace the old Bass implementation and its host bindings when the independent
path is integrated; do not ship parallel Legacy/New modes. Existing plugin identity
is unchanged, but POC Bass sound/automation/state compatibility is not promised.
The old coupled Accent/Decay/Drive mapping must be removed, not relabelled.
New Full-on Bass uses `com.ultimavox.fullon-bass` and its own explicit sound state.
This first DSP slice is not registered as an additional shipping instrument:
registration replaces the POC when all direct host bindings are available.
There is no source evidence that Gain changed pitch or velocity rewrote Attack;
those user examples describe prohibited behaviour, not a reproduced defect.

## Direct parameter contract

The following order is frozen for the new payload; future changes require a
new version. Times are milliseconds. Cutoff is the base cutoff before explicit
key tracking. Output Gain follows all nonlinear stages. Velocity multiplies
amplitude after those stages; it does not change pitch or envelope settings.

| ID | Unit | Range | Default |
| --- | --- | --- | --- |
| filter.cutoff | Hz | 35–12000 | 550 |
| filter.resonance | normalized | 0–1 | 0.55 |
| pitch.amount | semitones | 0–24 | 9.6 |
| pitch.time | ms | 1–100 | 18 |
| amp.attack | ms | 0.1–100 | 0.8 |
| amp.decay | ms | 1–500 | 60 |
| amp.sustain | normalized | 0–1 | 0.5 |
| amp.release | ms | 1–250 | 28 |
| osc.drive | multiplier | 1–6 | 2.6 |
| filter.drive | multiplier | 1–5 | 2.28 |
| filter.keytracking | normalized | 0–1 | 0.205 |
| output.gain | dB | -60–6 | -2.85 |
| pitch.curve | exponent | 0.25–8 | 2 |

Each setter accepts finite plain values and clamps only its own value. Unknown
IDs and nonfinite inputs fail without mutation. State is exactly 13 IEEE-754
binary32 values encoded little endian (52 bytes), with schema version 1 outside
the payload. Loading validates the complete candidate before committing. An
empty version-1 payload means factory defaults. No voices/runtime state persist.

## Host integration design (not yet implemented)

Use named, immutable host ParameterIDs derived from slot/instrument/parameter
identity with real units and meaningful ranges. The rack remains descriptor
driven. Remove the old Bass macro-to-sound mapping. Do not add compatibility
adapters for the rejected POC. Deliberate state/automation breaks must be versioned
and reported explicitly rather than loading old bytes under new meanings.

Direct values are base parameters; explicit modulation applies afterwards without
writing back to other host parameter values. Only real user-assignable Macros
remain as macros. Preset loads update direct host values and persistent base state
together; preset selection is not an automatable competing writer. State restore
and automation replay must use the same parameter model as the UI. Freeze the
complete host/state contract and test failure behaviour before registering Bass.

ADSR and pitch-envelope duration are latched on NoteOn. The current JUCE ADSR
contract explicitly prohibits changing its parameters during playback, so ordinary
edits update the next note without corrupting the sounding envelope. Continuous
filter/key tracking/drive/output changes apply to the sounding voice. Output gain
uses JUCE SmoothedValue with a five-ms ramp, after nonlinear processing. This
ramp changes amplitude, not envelope parameter values or oscillator pitch.

Continuous edits are published through fixed allocation-free block snapshots.
Only the audio thread touches a live voice. No locks, voice reconstruction or
state serialization in the callback. The isolated DSP below assumes the same
single-audio-thread ownership as InstrumentInstance; it is not a UI-thread API.

## Verification gates

DSP: mutate every parameter and prove all other saved values unchanged; reject
invalid state atomically; render gain/velocity scaling after distortion; compare
next-note ADSR updates and continuous filter/gain updates; exercise note release. Generic
instrument compliance and event validation cover bounds/lifecycle.

Integration: automation record/replay, preset vs automation precedence, versioned
state validation/rejection, save/reopen and explicit modulation ownership. UI: independent
labels/units/gestures/reset and reverse host automation. Windows CI and independent
Architecture Guard/realtime review precede merge. Actual listening, DAW automation
and visual acceptance remain integrated-milestone gates, not per-PR owner tasks.

## Primary implementation references

- [Steinberg parameters and automation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Parameters%2BAutomation/Index.html): normalized host values, named IDs/units, editing gestures and no automation parameter rewriting another.
- [JUCE ADSR](https://docs.juce.com/master/classjuce_1_1ADSR.html): sample-rate setup, note lifecycle and no parameter mutation during playback. Verified against the pinned JUCE 9.0.1 header as well.
- [JUCE SmoothedValue](https://docs.juce.com/master/classjuce_1_1SmoothedValue.html): existing ramp implementation to avoid abrupt gain changes.

Parameter independence is a technical gate, not proof of finished Full-on sound.
Anti-aliasing, modulation/smoothing quality, factory presets and listening remain
separate DSP acceptance work; the existing oscillator is not certified production
quality by the tests in this slice.
