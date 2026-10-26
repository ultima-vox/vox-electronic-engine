# Acid — authoritative UI → DSP / automation control matrix

Reference: owner's unannotated 1024×768 screenshot (2026-10-26); annotated image defines Rack (red) / Acid (green) boundaries. This is a *future required behavior* ledger. The presence of a label or an engine ID does **not** imply the control currently works in the plugin.

**Definitions**: `S` = current semantic `AcidEngine::parameterIds` in PR #59; `HOST` = dedicated stable per-slot VST3 parameter and automation to be implemented; `PERSIST` = Acid-local versioned state to be implemented. The old eight host macro IDs remain unchanged. `MISSING` = displayed function needs real model/DSP implementation. All rows require preset/state round-trip and real effect.

## Screen hierarchy

- **Shared shell, outside Acid ownership**: product logo, global project preset/save, seed and panic, CPU/MIDI indicators, 16 real Rack slot cards with selected state, channel, enable, artwork, and keyboard footer. Must use live Rack state, never four hardcoded demo slots.
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
| MOD/ENV2 | Target, Amount, envelope controls | MISSING separate ENV2 | Independent state/DSP, not ENV1 reused |
| MOD/LFO1–3 | Rate, shape, sync, depth, target as appropriate | MISSING three distinct LFOs | Each LFO modulates independent/assigned target, is host-sync capable and persisted |
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

Current PR #59 implements 36 DSP targets but exposes only frozen eight host macros; its extra descriptor parameters are not currently independent host automations. Gemini's preview AcidPanel is stored on `backup/gemini-acid-2026-10-26` but **not yet wired** to the recovered product editor. Current CI at PR #59: 28/30 tests pass, Rack integration crash and CH2 isolation failure; do not merge. In particular:

- No `AcidEngine*` direct UI binding to replaceable Rack runtime instance.
- No silent aliasing to host macro `Space`/`Motion`.
- No imaginary `Step.note`, `Step.slide`, `Step.octave`, `Sequence.stepCount`.
- No preview-only control marked production complete.
- No evidence-free claim of playable Acid or DAW acceptance.
