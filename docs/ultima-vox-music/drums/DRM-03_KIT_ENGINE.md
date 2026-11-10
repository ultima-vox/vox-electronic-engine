# DRM-03a: the VOX Drums kit engine (16 pads, 8 models)

Status: on `drm/03-kit`, stacked on DRM-02. This is the **engine slice** of
DRM-03. The plugin still plays the wrapped legacy module.

DRM-03b, the next PR, switches the processor to this engine and removes the
legacy copy. It also brings the parameter map, state and editor for 16
pads and 8 models.

## What it is

`products/drums/engine/KitEngine.{h,cpp}` (`vox::drums::kit`) is a C++
engine with no legacy ABI. Its synthesis is a line-by-line port of the
reviewed module (`VoxFullOnDrums.c` @ f9c8bc6), generalised.

| | Legacy module | Kit engine |
|---|---|---|
| Pads | 8, fixed types | **16**; any pad plays any of **8 models**: Kick, Snare, Clap, Hat, Tom, Rim, Cymbal and Perc (new) |
| Notes | fixed 36/38/39/42/46/45/37/49 | any note per pad; the lowest pad wins a shared note |
| Voices | 16, one per pad (a retrigger fades the previous hit) | **64**, up to **8 per pad** (polyphony 1–8 per pad); oldest stolen |
| Choke | hats only, fixed group | any pads, any group; on a coincident hit the lowest pad of the group wins (the legacy rule: closed hat cuts open hat) |
| Kick body (pitch env, drive, sub, tail) | kit-wide | per pad (Kick model) |
| Samples | 1 player per pad | **2 players (A, B)** per pad: *A only*, *velocity switch* at a split, or *mix*; Sample / Layer source as before |
| Outputs | main + 2 sends + Out 2..8 | same, unchanged |
| Block size / MIDI | ≤ 8192 frames, ≤ 512 events per call | any block size, any event count (no chunking at the plugin) |
| Sequencer | 8 lanes, 16 steps × 4 bars | 16 lanes (patterns as before; editing is DRM-04) |

**Perc** is the only new sound. It is a bounded two-operator FM blip plus a
band-passed noise burst. Character moves from a pure tone to a bright,
noisy zap; the FM index decays and is capped.

**Realtime:** `process()` allocates nothing, takes no lock and does no I/O;
every loop is bounded. Settings are plain structs, copied by the caller
between blocks.

**Settings are sanitised** (review 6737829467). At each block start the
engine copies the settings into a fixed-size member and brings every field
into range:
- In-range values stay bit-identical, so the parity results do not change.
- NaN takes the default; infinities and other out-of-range numbers are
  clamped to the nearest bound.
- An enum outside its set takes its first value.
- An output bus outside 0..7 plays on **Main**.
- A sample region is clamped to 0..1.

The aux index is also bounds-checked at the write. The original defect was
that `output = 8` wrote past the stack bus array: ASan reported a
stack-buffer-overflow.

## Evidence (local, Linux GCC 13 Release): 26/26 suite tests pass

| Test | Proves |
|---|---|
| `suite_drums_kit_parity_tests` | **108 renders bit-identical to the DRM-00 reference on all 10 buses** (main, Send 1/2, Out 2..8), comparing the legacy kit (`legacyKit()`, 16 voices, one voice per pad) with the reference module given the same values. It covers 12 scenarios (listed below) × 44.1/48/96 kHz × 64/333/512 frames. |
| `suite_drums_kit_engine_tests` | All 8 models sound on all 16 pads. At tune ±12 st, fine ±100 ct and character 0/1, at 44.1 and 96 kHz, every model is finite and bounded. Perc's character changes its timbre. Notes can be remapped; a shared note goes to the lowest pad. Choke groups work on any pads and can be disabled. Polyphony 1/2/4/8 holds as many hits; 128 hits fill exactly 64 voices. Sample players: A only, velocity below and from the split, mix, and both players reported in use. Output is bit-identical with 64/512/20000/37000-frame blocks. The allocation probe counts 0 allocations in `process()` with poly 8, sequencer, MIDI and panic. |

The 12 parity scenarios:
- defaults, normal and dense MIDI;
- samples;
- the internal sequencer in Internal and Both mode, with a loop back;
- 6 random kits, with every legacy parameter random within its range:
  - filter types, transient, output bus, sends, kick body, choke, velocity;
  - automation at block starts;
  - CC120/CC123, panic, release stop/fade.

**Mutation checks:** a factor of 1.0000001 on the Send 1 path makes 54
renders fail; a change of the tom click constant makes 72 renders fail.

## DRM-03b: the plugin on the kit engine

Status: on `drm/03b-plugin`, stacked on DRM-03a. The processor now runs
`kit::KitEngine` (64 voices); the wrapped legacy module, `DrumEngine` and
`engine/legacy8/` are removed (the frozen reference stays under
`tests/suite/legacy-reference/` for the parity tests only).

### Parameter map (contract change, needs Architecture Guard review)

**119 host parameters** (`products/drums/parameters.csv`, regenerated):

| Scope | Parameters | Count |
|---|---|---|
| per pad 1–16 | Level, Tune (±24 st), Decay (0.01–10 s, log), Pan, Cutoff (20 Hz–20 kHz, log), Mute | 96 |
| kit | Kit Volume, Velocity Sens, Choke Groups | 3 |
| sequencer | Seq Mode, Pattern, Seq Swing, Seq Accent, Lane 1–16 | 20 |

Host names are pad-numbered ("Pad 1 Level"), since a pad's model can
change. Every DRM-01/02 ID that stays keeps its identity (`drm.p01.level`,
`drm.kit.*`, `drm.seq.*`, `drm.seq.lane.p01`..`p08`).

What changes against DRM-01/02 (not released; no merge to main yet):
- **109 of the 164 IDs leave the host map** (55 stay): character, fine,
  resonance, filter, transient attack/sustain, source and the six
  `sample_*` of pads 1–8 (13 × 8), and
  the kick body (`drm.p01.pitch_amount`, `pitch_decay`, `drive`, `sub`,
  `tail`). They are now structural settings of a pad (versioned section
  `DPAD`), edited in the editor; a DRM-01/02 project migrates them (below).
- **Ranges widen**: Tune ±12 → ±24 st, Decay 0.08–1.5 → 0.01–10 s (all
  models share one range). Saved plain values restore exactly; host
  automation written against DRM-01/02 normalised values would map
  differently. There is no released automation to keep.
- **Sends** (`send1`, `send2`) are deferred to DRM-06 with the FX returns:
  without returns they would be dead controls. Output-bus routing is DRM-06
  as well (the engine already supports both).

### State

| Section | Content |
|---|---|
| `DPRM` (unchanged format) | host parameter plain values by ID; unknown IDs ignored |
| `DPAD` v1 (new) | per pad: model, note, choke group, polyphony, filter, source, sample mode, velocity split, fine, character, resonance, attack, sustain, kick body; players A and B (start, end, level, loop, reverse, normalize). Strict: a wrong version or size, NaN, or any value outside its range rejects the document; the live state is kept. |
| `DSMP` v2 | 32 sample slots (pad × 2 + player). v1 (DRM-02: 8 pads) is read onto player A of pads 1–8. |

A document without `DPAD` (DRM-01/02) takes the default kit and migrates
the former structural parameter values of pads 1–8 from `DPRM` onto the
pads and player A. Restore stays three-phase (review 6733006001): sections
are committed under the state lock, host notifications run without it.

**Threads:** structural settings live in `KitStore` behind a SpinLock held
for a struct copy. The audio thread takes them with a try-lock only when
they changed; if the lock is busy it keeps the previous block's settings.

### Editor

4 × 4 pad grid (model, note, M when muted; click auditions and selects).
Pad panel: model, note, choke group, voices, mute; level, tune, fine,
decay, pan, character; filter, cutoff, resonance, attack, sustain; kick
body row only for the Kick model. Sample panel: source, players mode, A/B,
load / clear / to library, waveform, start, end, level, split, loop,
reverse, normalize. Kit and sequencer controls; 16 lanes. The final skin is
DRM-07.

### Evidence (local, Linux GCC 13 Release): 26/26 suite tests pass

| Test | Proves |
|---|---|
| `suite_drums_parity_tests` | **81 plugin renders bit-identical to the DRM-00 reference** (9 scenarios × 44.1/48/96 kHz × 64/333/512 frames: host MIDI, automation of every shared host parameter, every pattern Internal/Both, loop back, samples on pads 1–8 with loop/reverse/normalize). The reference values come from the plugin's host parameters plus its `KitStore`. The voice pool is set to the reference's 16 (`setVoiceLimitForTesting`); with 64 voices, dense 96 kHz/64-frame scenarios differ by design (no stealing). Also: the 5 ported factory patterns equal the reference's content API cell by cell; a 600-event burst on one sample. |
| `suite_drums_product_tests` | 119-parameter contract and golden CSV; host defaults = default kit. Editor: every host parameter bound both ways; every structural control moves `KitStore` and follows it on the next editor tick (pads 1, 7, 14, players A and B); kick body row shows only for Kick. Sound: all 16 pads from their notes; level 0 and Mute silent (and not lit); 8 models on pads 3 and 13 from a reassigned note; a choke group on pads 10/11 with Choke Groups on/off. State: random `DPAD` round trip byte-identical and played; DRM-01/02 document migration; 9 malformed `DPAD` variants rejected with the live state kept; reentrancy and concurrent restore/save. Allocation probe. |
| `suite_drums_sample_tests` | Slot API (pad × 2 + player); player B restored as its own sample; DSMP v1 onto player A; editor A/B: load, status, start knob and Clear act on the selected player only; retirement, stale loads, budget, stress. |
| `suite_vst3_host_tests` | the built VST3 through JUCE's VST3 host: note sounds, "Pad 1 Level" at 0 silences, state round trip. |

**Defect found by the new tests and fixed:** a muted pad still lit its pad
button (the hit mask ignored Mute).

**Review 6743099574 (fixed):** an editor audition was sent as a Note On of
the pad's note, so on a shared note it played the lowest pad of that note,
and in Internal mode it was dropped. An audition is now a direct pad
trigger (`kit::Event::pad`): it bypasses the note map and the sequencer
mode; external MIDI keeps the lowest-pad rule. Regression: pads 10/11 on
note 60 with different models, each audition equals a render with the other
pad silenced, in Host MIDI and Internal mode; external note 60 plays only
pad 10. With the old routing 3 of these checks fail.

## Owner decisions (#120, comment 6749703618)

1. **Host automation contract accepted: 119 parameters.**
   - `products/drums/parameters.csv` is the 1.0 contract. Its IDs, ranges,
     normalisation, order and the state format (DPRM, DPAD v1, DSMP v2) are
     frozen before DAW acceptance. A later breaking change needs a versioned
     migration.
   - The 109 former IDs stay structural `DPAD` state. They are not dummy
     host parameters.
   - DRM-01/02 saved states migrate deterministically from `DPRM` to `DPAD`.
   - **Not compatible:** *normalised host automation* written against
     DRM-01/02 is not compatible for the removed IDs (they no longer exist
     as host parameters) or for Tune and Decay (their ranges changed). Saved
     plain values of every kept parameter restore exactly.
   - Covered by tests: the golden `parameters.csv`, 119 unique IDs with
     their defaults, the DRM-01 IDs kept, state round trip and DRM-01/02
     migration (`suite_drums_product_tests`).
2. **Pads 9–16 accepted** with the factory defaults Perc on notes 50–57,
   named "Perc 1".."Perc 8". They are full instruments. Covered by tests:
   - each one's audition sounds and lights only that pad;
   - its host note plays it alone;
   - distinct structure and host values survive save and restore, and each
     restored pad plays from its restored note.

   The export of these pads is tested in DRM-04a.
3. **Decoded sample budget: a hard cap of 512 MiB**
   (`SampleLimits::maxTotalBytes`). This follows review 6750424931 (P1),
   which found that the earlier version allowed a bounded overshoot.

   **What counts.** At every moment, `committedBytes()` is the sum of:
   - every slot's buffer, counted once, including the slot a load will
     replace;
   - every retired buffer a voice may still read;
   - the reservations of decodes in flight.

   It never exceeds the budget.

   **How each operation stays inside it**
   - **Load.** A decode reserves its exact size against `committedBytes()`
     *before* it allocates. The replaced buffer is retired at commit and
     still counts until it is reclaimed. A load that does not fit is
     refused, and the slot keeps its buffer, reference and sound.
     Consequence: replacing a sample needs room for both copies, or a Clear
     first.
   - **Restore.**
     - A sample the library already holds with the same content is reused
       with no new memory: restoring the current project costs nothing, and
       neither does the same sample on two slots.
     - Other samples reserve their memory next to the current generation.
     - A sample that does not fit is **deferred**: its reference is kept and
       saved back unchanged, its status is "waiting for memory", and the pad
       plays its synthesis meanwhile. It loads in the background once the
       replaced samples are reclaimed (`collect()`, every 100 ms).
     - A restore rejected later (for example a malformed `DPAD`) releases
       its reservation.
   - **Audio callback.** Unchanged: no allocation, lock or I/O.

   **Tests** (`suite_drums_sample_tests`, `testHardBudget`). Peak decoded
   memory is measured as the process-wide high-water mark of decoded bytes:
   - **Same-size replacement with a held voice:** refused, sound unchanged,
     peak within the cap. After Clear and reclamation the new sample loads.
   - **Full project restore while voices hold the old generation:** one
     sample loads, one is deferred and reported. The project saves back
     byte-identically. When the voices end, the deferred sample loads. Peak
     stays within the cap throughout.
   - **Restoring the loaded project:** no decode, no new memory.
   - **Concurrent loads:** of two that fit only one at a time, exactly one
     commits (20 rounds).
   - **Rejected restore:** its reservation is released.

   **Mutation checks**
   - Reservations not counted: 25 failures.
   - Rejected restore not releasing its reservation: 1 failure.
   - Removing the restore's size pre-check alone is still caught by the
     decode reservation. The pre-check only avoids reading a file that would
     be refused.
