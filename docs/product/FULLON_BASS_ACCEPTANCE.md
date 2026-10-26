# Full-on Bass: complete accepted control surface

Owner correction, 26 October 2026. Visual authority: `psy-bass-accepted(1).png`,
including the panel crop attached as `изображение(8).png`.

This document supersedes the simplified 13-control Sound composition as the
finished product target. The old screenshot is a visual reference, not evidence
that any feature exists in DSP. No reduced alternative, placeholder control or
renamed macro satisfies acceptance. Work may be integrated incrementally, but
Full-on Bass remains incomplete until the entire surface passes its gates.

## Required control inventory

| Section | Required controls | Required behaviour |
| --- | --- | --- |
| Oscillator | OSC 1/2/3 selectors; power; waveform/wavetable selector and previous/next; Octave, Semitone, Blend, Phase; waveform display | Three actual independently stored oscillator banks. Switching tabs selects their controls without overwriting another oscillator. Tuning affects pitch only; Blend mixes documented source components; Phase controls note-start phase. Display derives from the actual source. |
| Filter | Power; type selector; Cutoff, Resonance, Key Track, Env Amount; response display | Actual selected response, including the pictured 24 dB low-pass. Hz cutoff, Q/resonance, explicit note tracking and envelope depth are separate. Response display uses actual filter coefficients/model. |
| Amp envelope | Independent Attack, Decay, Sustain, Release; envelope display and pictured add/menu action | Real envelope, documented retrigger/legato and automation behaviour, no hidden coupling. Menu has a defined working action or the reference is explicitly revised with the owner. |
| Drive / Character | Power; model selector; Drive, Tone, Presence, Mix | Independent drive amount, post-drive tone filter, presence EQ and dry/wet balance. Mix=0 is the documented dry path; Mix=1 is wet. Tone/Presence do not rewrite main-filter values or tuning. Models and bypass are real processing paths. |
| Accent | Power; Amount, Vel→Cutoff, Vel→Drive, Accent Tone | Separate amount and explicit named velocity depths. They may modulate effective DSP values, never rewrite another base/automated value. Exact Accent event source and Tone destination must be documented and tested before implementation is accepted. |
| Performance | Portamento, Unison, Detune, Spread | Actual glide, bounded unison voice count, detuning and stereo distribution. Spread changes stereo placement without rewriting tuning/Detune. Glide has a documented mono note-priority/legato policy. |
| Modulation | Power; ENV1, ENV2, LFO1, LFO2, LFO3, STEP; selected-source controls/display; envelope destination selector and independent ADSR | Separate persisted source banks. They are real sources, not GUI tabs over one envelope. LFO controls and STEP editor have an explicit complete specification. Timing/sync and audio-block independence are tested. |
| Matrix | Add/remove route; source, signed depth and destination for each row; active-route count | Inspectable, disableable modulation, including pictured Mod Wheel, Aftertouch, LFO1, ENV2 and Velocity sources. Destination remains a base parameter plus effective modulation; no cross-writing APVTS values. Bounded route budget and overload/failure behaviour. |
| Keyboard/performance bar | Keyboard, Pitch wheel, Mod wheel; Keyboard/Chords/Scale pages; velocity curve; MIDI Learn; keyboard action button | Real MIDI performance paths and defined scale/chord/learning behaviour. No decorative enabled actions. Note ownership, panic, focus loss, automation and state restore are verified. |

The pictured selected banks expose **28 rotary positions** (six groups of four
plus four selected-envelope controls) and two wheel positions. That is not the
number of independent stored parameters: reused oscillator/modulation banks,
selectors, switches and matrix state add further controls. The initial 32-value
bound must not be frozen for this complete target. Resource bounds are reviewed
against the full inventory before host/state contracts are finalized.

## Implementation rules

- Keep the accepted dark VOX surfaces, cyan arcs, density, grouping and keyboard.
  Render native vector components; do not use a bitmap as an interactive skin.
- Each ordinary knob has one stable parameter ID, label/unit/range/default,
  gesture/reset/text input, automation path, persistent base value and DSP owner.
- Explicit velocity/other modulation changes effective values only. Its source,
  destination and depth remain visible; no hidden parameter rewriting.
- Use established DSP primitives and documented algorithms. Never add a bespoke
  envelope merely to bypass JUCE's stated lifecycle contract. State explicitly
  whether continuous automation affects a held note or the next note.
- Schema and plugin-format changes are intentional POC breaks authorized by the
  owner; no parallel legacy Bass mode or old-project migration is needed.
- Missing functionality is a completion blocker, not a simulated knob, an
  undocumented fallback or a reason to lower the accepted scope.

## Required evidence for every control

1. **Isolation:** change its stored value and prove unrelated base values stay
   fixed; cover all banks and slots, not only the selected GUI page.
2. **Signal behaviour:** render meaningful A/B cases showing the intended audio,
   timing or stereo change. Test dry/wet/bypass endpoints and valid dependency
   conditions. Merely saving a different float is insufficient.
3. **Host/UI:** correct gestures, units, text entry, defaults and automation in
   both directions; preset loads and host replay use the same model.
4. **Restore:** save/reopen preserves actual patch and assignments; malformed
   loads fail transactionally; no stale preset name silently substitutes sound.
5. **Realtime/lifecycle:** bounded memory/work, no callback allocations/locks,
   note release and churn safety; performance measured against declared workload.
6. **Visual/musical:** actual editor captures match the accepted reference;
   listening confirms usable Full-on bass, parameter transitions, aliasing/noise
   quality and factory content. Actual Cubase/REAPER evidence at the agreed
   integrated milestone, not after every PR.

## Current gap statement

The 13-parameter DSP core and the in-progress direct host path do **not** meet
this acceptance. Three oscillator banks, the complete Drive/Character block,
Accent, unison/stereo/glide, complete source banks and complete matrix/performance
UI remain incomplete. Existing block-level Rack envelope/LFO helpers are not
substitutes for the pictured dedicated synthesis sources. The simplified native
Sound page is WIP and must not be reported as the accepted interface.

Release decision: **DO NOT RELEASE** until the complete inventory is implemented,
reviewed and passes the applicable automated and integrated manual gates.
