# Electronic Drums — design (PROPOSED)

Status: PROPOSED, owner decision of 31 Oct 2026 (chat). Not merged; each
phase below is its own Draft PR with tests and exact-head Windows CI, and
needs owner approval before it enters `integration/rack-1.0`.

## Owner decisions this design follows

| Question | Decision |
| --- | --- |
| Target | One **Electronic Drums** instrument following the owner's visual mock-up (SOUND page: pad bank, sample, amp envelope, filter, transient, pitch, performance, step sequencer, pattern tools, global, drum map, output, MIDI learn). Styles: psy, full-on, goa, progressive, hardcore and more |
| Sounds | **Rendered from our own synthesis** into sample buffers (no third-party WAV, no licence risk). The sample player, waveform, reverse / normalize / one-shot / loop work on these buffers |
| Engine | **Native module** (extends the Full-on Drums C code), shipped as one signed `.vins` |
| Automation | Key per-pad controls and kit/sequencer controls are host parameters (pool of 128 per slot). Everything else is **UI-only** state of the slot: editable in the editor, saved with the project, not automatable |
| Order | Start now; the Full-on Drums acceptance (#93) is no longer the gate for this work |

Removed from the mock-up by the owner: Chords / Scale, pitch and mod wheels.

## Identity

- New instrument `com.ultimavox.electronic-drums`, package
  `VoxElectronicDrums.vins`. Full-on Drums (`com.ultimavox.fullon-drums`)
  keeps its id, parameters and state, so existing projects still open. Its
  sound becomes the **Full-on** kit of the new instrument.
- One slot, one MIDI channel, eight pads (as now).

## Pads and drum map

Pads, in mock-up order: Kick, Snare, Clap, Closed Hat, Open Hat, Perc 1,
Perc 2, Rim / FX. The **Drum Map** selects the input notes:

| Map | Kick | Snare | Clap | CH | OH | Perc 1 | Perc 2 | Rim / FX |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| General MIDI (default) | 36 | 38 | 39 | 42 | 46 | 45 | 47 | 37 |
| Chromatic C1 | 36 | 37 | 38 | 39 | 40 | 41 | 42 | 43 |
| User | any note per pad, set in the editor, saved/loaded/reset |

MIDI export uses the active map. Choke: Closed Hat cuts Open Hat (switchable).

## Sound engine (per pad)

1. **Sample source.** Each pad picks one sample from its library (e.g. Kick:
   Psy Punch, Full-on, Goa Round, Prog Deep, Hardcore Gabber, Hardstyle,
   Techno, 909-style, Sub; Hats: closed/open metal and noise variants ...).
   Every sample is rendered from a synthesis model **once per sample rate,
   off the audio thread** (at `prepare`), into one shared, read-only cache
   for all instances. Selecting a sample on the audio thread only switches a
   pointer. Budget: about 100 samples, about 20 MB at 48 kHz; to be measured.
2. **Playback.** Pitch Tune (±24 st) and Fine (±100 ct) by interpolated
   resampling. Modes One Shot / Loop, Reverse. Normalize applies a gain
   computed once from the rendered sample.
3. **Amp envelope** A / D / S / R (one-shot pads use A/D/R; S applies in Loop).
4. **Filter** on/off, LP 12 / LP 24 / HP 12 / HP 24 / BP, Cutoff, Resonance
   (SVF; 24 dB = two stages).
5. **Transient** on/off, Attack and Sustain (envelope-follower shaper).
6. **Performance** Pan, Level. **Send 1 / Send 2 and Out 1 / Out 2 per pad
   need per-pad outputs from the module into the Rack** (today a slot has one
   stereo output and the Rack's sends act per slot). That is phase 6, an
   architecture change; until then the per-pad send / output controls are
   **not shown** (never inert).
7. Voices: 16, with steal oldest and a 3 ms fade (as now); velocity
   sensitivity; choke group.

## Sequencer

Rack-owned 8 × 16 pattern, 1–4 bars (content ABI #74), as now, plus:
- per lane: **Mute, Solo, Velocity scale, Probability** (UI-only state, applied by
  the module when it plays the pattern; seeded, so export stays reproducible);
- global: **Swing, Groove, Humanize, Probability**, Length, Fill, Random;
- pattern tools (editor operations on the Rack pattern, no ABI change):
  Copy, Paste, Random, Humanize, Clear, Shift;
- **Play** in the editor = transport preview when the DAW is stopped;
  **Tempo** applies only to that preview (the DAW tempo wins while playing).

## Host parameters (pool, ≤ 128) vs UI-only state

| Host parameter (automatable) | Count |
| --- | --- |
| Per pad: Level, Pan, Tune, Decay, Cutoff, Resonance | 6 × 8 = 48 |
| Kit: Volume, Velocity Sens, Hat Choke | 3 |
| Sequencer: Mode (Host / Internal / Both), Swing, Groove, Humanize, Accent, Probability | 6 |
| Lane mute × 8 | 8 |
| **Total** | **65** |

UI-only (saved with the slot, not automatable): sample choice, reverse,
normalize, play mode, A / S / R, fine, filter on/type, transient on / attack /
sustain, pad on, per-lane solo / velocity / probability, drum map, pattern
length, preview tempo.

## Contract changes (need review)

1. **UI-only parameters (phase 1).** `INSTRUMENT_MODULE_ABI.md` already allows
   descriptors with `VOX_PARAM_HOST_EXPOSED` cleared and `poolIndex = -1`,
   but the native adapter rejects them today. Phase 1 implements them:
   - the adapter imports them;
   - the Rack keeps their values per slot and hands changes to the audio
     thread lock-free, at block boundaries like pool values;
   - the module saves them in its own state blob (no Rack schema change);
   - the editor binds controls to them through the Rack, not APVTS.
   
   Tests: import, edit while playing, save/reopen, missing-module restore,
   no allocation.
2. **Waveform display.** The editor renders the selected pad once through an
   offline instance of the same module on the controller thread (as MIDI
   export already does). No ABI change.
3. **SOUND layout schema 2.** It adds widget kinds `padBank`, `waveform`,
   `envelope`, `filterCurve`, `stepSequencer` and per-pad sections that
   follow the selected pad. Schema 1 layouts (Full-on Drums) keep working.
4. **Per-pad outputs (phase 6).** The module writes 8 stereo pad stems; the
   Rack offers per-pad sends and outputs. This is a separate architecture
   review.

## Phases (each a Draft PR on `integration/rack-1.0`)

| # | Scope | Depends on |
| --- | --- | --- |
| 1 | UI-only parameters: adapter, Rack, persistence, editor binding | — |
| 2 | Electronic Drums module: sample cache, player, envelope, filter, transient, 8 pads, drum map, Full-on kit parity; `.vins` | 1 |
| 3 | Kits / styles: psy, full-on, goa, progressive, dark, hi-tech, hardcore, hardstyle, techno, more; factory patterns per style | 2 |
| 4 | Sequencer extensions and pattern tools; preview Play / Tempo | 2 |
| 5 | SOUND layout schema 2 and the mock-up editor; MIXER tab; Drum Map save/load; MIDI Learn | 1–4 |
| 6 | Per-pad outputs / sends into the Rack (Out 1 / Out 2, Send 1 / Send 2) | architecture review |

Every phase keeps the realtime rules: no allocation, lock, I/O, logging or
rendering in the audio callback. CI green is not DAW acceptance.
