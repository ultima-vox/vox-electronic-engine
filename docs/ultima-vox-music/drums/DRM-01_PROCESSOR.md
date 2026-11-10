# DRM-01: VOX Drums processor, parameters, state and Main editor

Status: implemented on `drm/01-processor` (stacked on DRM-00). Automated
evidence only. Automated tests do not establish DAW routing, sound quality or
visual acceptance. Cubase and REAPER acceptance happens once, on the finished
instrument (DRM-08, owner decision of 2026-11-08).

## What the plugin does now

- **Sound:** the eight synthesized pads of the reviewed legacy engine:
  Kick 36, Snare 38, Clap 39, Closed Hat 42, Open Hat 46, Tom 45, Rim 37 and
  Cymbal 49. Output is stereo on the main bus.
- **Playing it:** host MIDI notes trigger the pads. CC120 stops all voices
  and CC123 fades them out. The editor's pad buttons audition a pad.
- **Internal sequencer:**
  - five factory patterns: Full-on Basic, Full-on Driving, Rolling Offbeat,
    Breakdown and Kick & Offbeat;
  - follows the host transport (tempo, ppq, play state, loop or seek back);
  - swing, accent and per-lane switches;
  - modes: Host MIDI, Internal or Both.
- **Parameters:** 108 host parameters with stable IDs `drm.*`; the list is
  [`products/drums/parameters.csv`](../../../products/drums/parameters.csv).
  - Per pad (`drm.pNN.*`): level, tune, fine, decay, pan, character, filter
    type, cutoff, resonance, transient attack and transient sustain.
  - Kick body: pitch envelope, pitch decay, drive, sub and tail.
  - Kit: volume, velocity sensitivity and hat choke.
  - Sequencer: mode, pattern, swing, accent and eight lane switches.
  - Values display in their real units: Hz/kHz, ms/s, st, ct, %, L/C/R and
    the drive factor.
  - Frequencies and times use a logarithmic knob response.
- **State:** VOXS schema 1. The `UIST` section holds the editor size. The
  `DPRM` section holds every parameter, saved by ID as a plain value.
  - Restore is transactional: a malformed section is rejected and the live
    state is kept.
  - Unknown IDs are ignored, values outside a range are clamped, and missing
    IDs take their defaults.
  - A restored state saves back byte-identically.
- **Restore order (review 6733006001):** no host notification or listener
  callback runs under the state lock.
  1. *Commit*, under the state lock: the UI state and the product sections
     the restored state will save are published together, and the restore
     is marked in flight.
  2. *Apply*, without the state lock: the parameters are set, which notifies
     the host and listeners. They may save state, from any thread.
  3. *Settle*, under the state lock: the applied values are recorded and the
     in-flight mark is cleared.

  A save while a restore is in flight writes the restored state, never a mix
  of old and new. Restores are serialised by a separate lock that saves never
  take.
- **Editor:**
  - pad grid with note numbers and hit lights (host MIDI, sequencer and
    audition);
  - the selected pad's controls;
  - a Kick body row when pad 1 is selected;
  - Kit and Sequencer sections;
  - a read-only step view of the selected pattern with the playhead.

  Every control is bound to its host parameter. Pattern editing is DRM-04;
  the full custom skin is DRM-07.

## Not in this build yet (no stubs, no hidden controls)

The engine parameters for these features stay at their engine defaults and
are not host parameters yet:

| Feature | Engine parameters | Arrives in |
|---|---|---|
| Pad source and sample settings | `*.source`, `*.sample_*` | DRM-02: done, see `DRM-02_SAMPLES.md` |
| Sends and separate outputs | `*.send1`, `*.send2`, `*.output` | DRM-06 |
| 16 pads, 8 models, 2 samples per pad | — | DRM-03 |
| Pattern editing, MIDI export | — | DRM-04 |

The product test checks this list: an engine parameter that is neither
exposed nor in this table fails it.

## Engine

`products/drums/engine/legacy8/` holds byte-identical copies of the reviewed
module at f9c8bc6 (see `PROVENANCE.csv`): `VoxFullOnDrums.c` and its ABI
header.
- They are compiled statically into the plugin, with the entry point renamed
  to `voxDrumsLegacy8EntryV2`.
- `DrumEngine` is the only code that sees that C interface (FOUND-00 K5
  option a). It is a private, transitional adapter, not a plugin ABI;
  nothing is loaded dynamically.
- `suite_boundaries` allows exactly these files.
- DRM-03 replaces the module with the general engine, with parity against
  the DRM-00 corpus.

Realtime contract:
- `processBlock` allocates nothing and takes no lock. All buffers are sized
  at construction.
- Parameter changes apply at block start.
- Host blocks larger than 8192 frames, and blocks with more than 512 events
  per engine call, are split at sample boundaries. Every event is delivered
  at its own sample, never before the start of its engine call (review
  6734386412).
- More than 512 events on a single sample are folded into at most 10:
  - one CC120 if any, one CC123 if any;
  - the loudest NoteOn per pad.

  This is exactly how the engine resolves one sample's events, so the
  result sounds the same. The one difference: repeated CC123 on that sample
  become a single one. Messages the engine ignores (NoteOff, other CCs) are
  counted as folded.
- At most 4096 MIDI events are accepted per host block; the rest are counted
  as dropped.
- `reset()` silences all voices.

## Tests (CTest label `suite`)

| Test | Proves |
|---|---|
| `suite_drums_parity_tests` | The plugin, through `processBlock`, is **bit-identical** to the frozen DRM-00 reference module driven through its own ABI. This covers 63 renders: 7 scenarios (host MIDI on all pads, random automation of every exposed parameter, each factory pattern in Internal and Both mode, swing/accent, loop back jump) × 44.1/48/96 kHz × 64/333/512 frames. |
| `suite_drums_product_tests` | **Parameter contract:** layout = `parameters.csv`, unique `drm.` IDs, host defaults = engine defaults, explicit deferral list. **Editor:** every parameter is reachable through a bound control, both directions; all controls fit at minimum and maximum size; pad audition and lights. **Sound:** every pad from its note; level 0 is silent; non-kit notes are silent; every pattern plays only while the transport runs; patterns differ; Host mode does not sequence; Internal mode ignores notes; 20000-frame block with 6000 events; `reset()`. **State:** exact round trip; a listener that saves during restore, on the same thread and on another thread it waits for, gets the restored state and does not deadlock; concurrent restore A/B with save gives only whole A or B; defaults when the section is missing; rejection of 7 malformed variants with the live state kept; unknown IDs; clamping. **Allocation probe:** 44.1/48/96 kHz × 32–2048 frames with transport, Both mode, MIDI, automation and audition. |
| `suite_shell_tests_drm` | The FOUND-01 contract, now with sound: output is finite, overwrites the host buffer and pads sound. Lifecycle at all rates and blocks, editor, transactional state, concurrent restore/resize/save. |
| `suite_vst3_host_tests` | Each built `.vst3` is loaded through JUCE's VST3 host (Steinberg hosting code), the way a DAW loads it. Checks: one instrument class with name and vendor; stereo out, no input, MIDI in. Drums: a MIDI note sounds and the host-visible "Kick Level" (DRM-03b: "Pad 1 Level") at 0 silences it. Synthesis/FX shells: silent. IComponent state loads into a second instance and saves back identically, including the host-changed parameter. Editor opens through the host where a display exists. |

The Steinberg validator in `suite.yml` runs on all three bundles as before.

Local runs (Linux, GCC 13, Release): all 23 suite tests pass. ASan/UBSan on
the Drums tests is reported in the PR.

`VOX_SCREENSHOT_OUT=<dir> suite_drums_product_tests <parameters.csv>` writes
the editor at default values (2x) for review.
