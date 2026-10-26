# Acid — authoritative UI → DSP / automation control matrix

Reference: owner's unannotated 1024×768 screenshot (2026-10-26); annotated image defines Rack (red) / Acid (green) boundaries. This is a *future required behavior* ledger. The presence of a label or an engine ID does **not** imply the control currently works in the plugin.

**Definitions**: `S` = current semantic `AcidEngine::parameterIds` in PR #59; `HOST` = dedicated stable per-slot VST3 parameter and automation to be implemented; `PERSIST` = Acid-local versioned state to be implemented. The old eight host macro IDs remain unchanged. `MISSING` = displayed function needs real model/DSP implementation. All rows require preset/state round-trip and real effect.

## Explicit responsibility boundary

**Implement ONLY Acid Synth.** The product Rack (red region in annotated user screenshot) is not Production-ready but must not be re-implemented as part of this Acid feature branch. Red/green annotated outlines are reference markup only. Acid's green workspace is the entire feature scope. Keep the existing shared header, slot rail, keyboard, transport, channel routing and global shell untouched. Minimal selected-slot integration in existing editor/processor is allowed; no Rack refactor or API redesign. Smoke/Rack tests are regression checks only.

## Screen hierarchy

- **Existing product shell and Rack, not a deliverable**: product logo, global project preset/save, seed and panic, CPU/MIDI indicators, 16 real Rack slot cards, global routing, and shared keyboard. Read their selected-slot state only; do not implement, restyle or duplicate these areas.
- **Acid header**: slot identity/name, local sound preset selector and prev/next/favorite, actual MIDI channel; navigation SOUND/PATTERN/ROUTING/ZONES/MACROS/ADVANCED uses existing product navigation.
- **Acid hero**: `ACID` + 303 instrument artwork, display-only identity without overlay marks; cyan controls, green Acid identity artwork.

## Synth controls (every knob independently testable)

| Visible area | Visible control | Semantic owner / ID | Required observable behavior |
| --- | --- | --- | --- |
| OSC | Waveform selector + Wave knob | S `waveform` | Same genuine oscillator shape/morph parameter, synchronized in both controls, no change to Tune/Fine/PW |
| OSC | Tune | S `osc_tune` | −24..+24 semitones, correct audio pitch |
| OSC | Fine | S `osc_fine` | −100..+100 cents, correct audio pitch |
| OSC | Pulse Width (PW) | S `osc_pw` | Duty cycle changes only for waveforms supporting PW |
| FILTER | Filter type selector | S `filter_mode` | LP24 / diode18 / BP12 / HP12 produce distinct measured responses |
| FILTER | Cutoff | S `cutoff` | 20Hz–18kHz mapping; response changes independently |
| FILTER | Resonance | S `resonance` | Changes filter resonance, bounded/finite under high Q |
| FILTER | Env Amt | S `envelope` | Changes filter envelope depth, not VCA ADSR |
| FILTER | Key Track | S `key_tracking` | Correct pitch-relative cutoff |
| ENVELOPE/AMP | Attack, Decay, Sustain, Release | S `amp_attack`, `amp_decay`, `amp_sustain`, `amp_release` | Separate 4 VCA stages, audible independent changes, curve follows actual values |
| ENVELOPE/FILTER | Attack, Decay, Sustain, Release | S `flt_attack`, `decay`, `flt_sustain`, `flt_release` | Separate 4 filter envelope stages; a tab switch changes attachment + value + curve |
| DIST | Mode selector | S `dist_mode` | Tube / soft clip / hard clip / wavefold switch actual algorithm |
| DIST | Drive | S `drive` | Input saturation amount, independent |
| DIST | Tone | S `dist_tone` | Post-distortion tone LP, independent |
| DIST | Mix | S `dist_mix` | Dry/wet, fully dry bypasses nonlinear gain |
| ACCENT | Amount | S `accent` | Velocity/step-accent modulation of VCA and filter |
| SLIDE | Time | S `slide` | Audible legato pitch glide where slide/overlap enabled |
| OUTPUT | Volume | S `output` | Output level only, no pitch change; correct dB display |
| OUTPUT | Pan | S `pan` | Output stereo pan only, center is neutral |
| MOD/ENV1 | Target, Amount, A, D, S, R | S `mod_target`, `mod_amount`, `mod_attack`, `mod_decay`, `mod_sustain`, `mod_release` | Per-source envelope routed to chosen target; curve reflects actual state |
| MOD/ENV2 | Target, Amount, Attack, Decay, Sustain, Release | `env2_target`, `env2_amount`, `env2_attack`, `env2_decay`, `env2_sustain`, `env2_release` (new IDs 40–45) | Implemented in branch: independent ADSR, own destination, host bindings and v5 module state; CI pending |
| MOD/LFO1 | Rate, waveform, depth, target | S `lfo1_rate`, `lfo1_wave`, `lfo1_depth`, `lfo1_target` (additive IDs 36–39) | Implemented in Acid branch: distinct 0.05–20 Hz bipolar LFO, 4 waves, independent destination, APVTS, source selector, v4 state; Windows CI confirmation pending. Host BPM sync remains outstanding. |
| MOD/LFO2 | Rate, waveform, depth, target | S `lfo2_rate`, `lfo2_wave`, `lfo2_depth`, `lfo2_target` (IDs 46–49) | Implemented in source: independent bipolar LFO2 DSP, host automation, own UI tab and v6 state. CI pending; host BPM sync missing. |
| MOD/LFO3 | Rate, waveform, depth, target | S `lfo3_rate`, `lfo3_wave`, `lfo3_depth`, `lfo3_target` (IDs 50–53) | Implemented in source: independent bipolar LFO3 DSP, host automation, own UI tab and v6 state. CI pending; host BPM sync missing. |
| MOD/STEP | Step modulator, sync, target, depth | MISSING step modulation source | Distinct from note sequencer; audible and stateful |
| PERFORMANCE | Portamento | S `portamento` | Glides by selected duration, independent of Slide Time semantics |
| PERFORMANCE | Unison voices | S `unison_voices` | 1–4 audible voices; correct normalization and CPU budget |
| PERFORMANCE | Detune | S `unison_detune` | Changes voice frequency spread without changing Tune |
| PERFORMANCE | Stereo Spread | S `stereo_spread` | Stereo width changes, mono fold-down safe |
| PLAY MODE | Mono/Legato/Unison mode selector | MISSING distinct mode selection | Selection changes real voice policy, not just label |
| PLAY MODE | Legato / Retrigger / Vel→Accent / Slide on Overlap | S packed `play_mode_flags` currently; requires separate host toggles | Each boolean independently automatable and saved, with stable bit-mask migration in Acid-local state |

## Step sequencer controls

| UI | Canonical model, ownership | Required behavior |
| --- | --- | --- |
| 16 step headers | Selected `slotId` + `sequence::Sequence::size()` | Correct playhead sync from host PPQ; no decorative false playhead |
| Note lane | `Step.noteOffset` relative to pattern root | Editable notes, note names match actual MIDI output |
| Accent lane | `Step.accent`; `Step.velocity` is float 0..1 | Accent is per step, velocity scaling only via intended rule |
| Slide lane | `Step.slideDuration` + `Step.gateWidth` | Legato overlap, release and MIDI export match playback |
| Gate lane | `Step.gate` | Gate mutes note; NoteOff always sent |
| Octave lane | part/root-relative `Step.noteOffset` ±12 semitones | Actual transposition; no nonexistent `Step.octave` field |
| Generate / Mutate / Clear | AcidGenerator + Rack slot sequence publication | Actual per-slot sequence update; stable deterministic sub-seed & preserve other slots |
| Play / Stop / Loop | Host transport / module sequencer state | Observable audio/MIDI transport, no local-only boolean |
| Pattern preset / Density / Variation | Acid generator config + selected slot | Generated pattern responds to each setting; reproducible |
| Swing / Resolution / Length | Persistent selected sequence timing state | Live playback matches MIDI export; 1/8, 1/16, 1/32 and length 8/12/16 in reference |
| Right-side lane selectors | Genuine lane edit destinations or explicit accessible labels | No fake dropdowns: implement semantics or remove unsupported affordances |

## Production acceptance / status

Current PR #59 has **54 Acid DSP semantic parameters** (36 original + LFO1 4 + ENV2 6 + LFO2/3 8). Acid-local schema v6 serializes all 54 and accepts v1/v3/v4/v5; absent sources default to depth off. Five UI modulation source tabs rebind the actual slot-scoped host parameters (ENV1, ENV2, LFO1, LFO2, LFO3). New audio/host/UI regression tests were added, **but their latest Windows CI result is not verified**. Last completed pre-v6 CI passed 29/30 tests, with `bass_render_diagnostics/save_restore_rerender` failing. STEP, host-BPM sync, final visual layout and DAW acceptance remain outstanding. Specifically:
- No `AcidEngine*` direct UI binding to replaceable Rack runtime instance.
- No silent aliasing to host macro `Space`/`Motion`.
- No imaginary `Step.note`, `Step.slide`, `Step.octave`, `Sequence.stepCount`.
- No preview-only control marked production complete.
- No evidence-free claim of playable Acid or DAW acceptance.

**Real screenshot verification:** Windows visual gate on run 40373368577 generated `acid-actual-editor-1024x768.png`, a JUCE capture (not HTML). It is a working integration screenshot but does not meet the owner's pixel/typography/hero reference. Rack is not Production-ready; its independent acceptance does not change the scope of PR #59.
