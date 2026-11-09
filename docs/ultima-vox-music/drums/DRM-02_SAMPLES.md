# DRM-02: VOX Drums pad samples

Status: implemented on `drm/02-samples`, stacked on DRM-01. The evidence is
automated only. Automated tests do not establish DAW routing, sound quality
or visual acceptance; Cubase and REAPER acceptance is done on the finished
instrument (DRM-08).

## What the user gets

- **Per-pad source:** each of the 8 pads can be *Synth*, *Sample* or *Layer*
  (synthesis plus sample).
  - Host parameters `drm.pNN.source`, `sample_start`, `sample_end`,
    `sample_loop`, `sample_reverse`, `sample_normalize` and `sample_level`.
  - 56 new parameters, 164 in total. All are non-automatable, as in the
    engine.
- **Loading a sample:**
  - Formats: WAV (16/24/32-bit PCM and float), AIFF, FLAC and Ogg; mono or
    stereo, at the file's own rate. The engine resamples at playback.
  - Three ways to load: the *Load* button, dropping a file on a pad, or
    dropping it on the waveform.
  - Decoding runs off the audio thread, on a background job.
- **Sample panel:**
  - source, Load / Clear / To library;
  - a status line with name, length, rate and channels, or why the sample
    is missing;
  - the waveform from a prepared overview, with the start/end region;
  - Start, End and Level knobs, and Loop / Reverse / Normalize switches.
- **When a sample is missing:** the pad plays its synthesis. The project is
  never silenced and no other file is substituted.

**Later action wins** (review 6734821926):
- Every pad keeps a request counter.
- These invalidate a pending load of that pad:
  - Clear;
  - a direct assignment;
  - a newer load;
  - a project restore.
- A decoded result is committed only if its request is still the pad's
  latest. Otherwise it is discarded, and the editor shows "superseded by a
  later action".

## Ownership and realtime (03_VOX_DRUMS_SPEC §5)

```
message thread                       audio thread
decode → SampleBuffer (shared_ptr)   acquire(): one atomic load of the current Table*
assign → new immutable Table(gen N)  engine reads views (id, channel pointers)
old Table, removed buffers → retired report(): seqlock write of {gen, ids held by voices}
collect() (timer, 10 Hz):            (wait-free, fixed size, no allocation)
  free Table   when reported gen ≥ N
  free buffer  when reported gen ≥ N, report complete, id not held
```

- The audio thread never allocates or frees memory, takes no lock and never
  touches a `shared_ptr`.
- Memory is always freed on the message thread, outside the library lock.
- A missing, stale or overflowing report frees nothing.
- `prepareToPlay` frees every retired item, because the audio thread is
  stopped then.

**Limits (provisional):**
- 60 s per file, at most 192 kHz;
- 512 MiB decoded in total;
- 1 GiB file size.

The decoded-memory budget is an **open owner decision** (03 §9). These
values are provisional and live in one place: `SampleLimits` in
`products/drums/SampleLibrary.h`.

## Project state

VOXS schema 1 gains the section `DSMP`. It stores references only, never
sample data. Each assigned pad saves:
- SHA-256 of the file bytes;
- frames, channels and rate;
- absolute path and file name.

**Restore**, transactional together with UIST and DPRM:
1. The saved path is tried first; the file is used only if its content
   hash matches.
2. Otherwise the user sample library is searched by hash.
3. If neither works, the assignment and its reference are **kept**.
   - The status says `not found`, `file content changed since it was
     assigned`, or the decode error.
   - A diagnostic line is written, for example `Pad 1 (Kick): 'kick.wav'
     not found (<path>, sha256 <12 hex>); the pad plays its synthesis`.
   - The same reference is saved back.

*To library* copies the file into the user sample library and points the
reference at the copy. The library is
`<user application data>/ULTIMA VOX/VOX Drums/Samples/<sha256>.<ext>`, or
`VOX_DRUMS_SAMPLE_LIBRARY` if that variable is set. A project made on this
computer then finds its samples even after the original files move.
Packing samples for another computer is DRM-05/08 (kit export).

Malformed sample sections are rejected and the live state is kept. That
covers an empty section, an unknown version, truncation, trailing bytes, an
invalid flag, bad UTF-8 and an invalid channel count.

## Tests

| Test | Proves |
|---|---|
| `suite_drums_sample_tests` | **Decoding:** 18 formats (16/24/32-bit × mono/stereo × 44.1/48/96 kHz) decode with exact values (within quantisation), frame count, rate and SHA-256. Errors are deterministic for a missing, empty or foreign file, a 61 s file, a 4 kHz file and an exceeded budget; a truncated file gives its finite readable part or an error. **Sound:** Sample without a sample is bit-identical to the synthesis; Synth ignores the sample; Sample follows the waveform (correlation > 0.95); Layer works; level 0 is silent; start and reverse work; all 8 pads play samples. **Ownership:** nothing is freed without a report; a buffer is freed after a report on the newer table; a buffer a looping voice plays stays alive until the voice ends; a storm of 400 replacements with voices on all pads leaves 0 retired items and 0 leaked buffers. **Realtime:** 0 allocations in `processBlock` with samples while samples are replaced between blocks. **State:** round trip; a missing file keeps its assignment, gives a diagnostic and saves back unchanged; a changed file is not used; library lookup by hash; 5 malformed sections rejected. **Editor:** status, buttons, drag targets, Clear. **Stale loads:** completions are held until after the later action, and the later action wins:
- Clear;
- a newer load, with completions in either order;
- a direct assignment;
- a project restore of an assigned pad and of an empty pad. |
| `suite_drums_parity_tests` | 81 renders **bit-identical** to the DRM-00 reference. New: 18 renders with samples on every pad (Sample and Layer, loop, reverse, normalize, automation of every parameter, a 44.1 kHz sample at 44.1/48/96 kHz hosts). The plugin's table, IDs and usage reports reach the engine exactly as the reference ABI does. |
| `suite_drums_product_tests` | Parameter contract with 164 parameters; every parameter, including the new sample ones, reachable through a bound control. |

A mutation check: if the "still held by a voice" condition is dropped from
`collect()`, "a buffer a voice still plays is not freed" fails.

`VOX_SCREENSHOT_OUT=<dir> suite_drums_sample_tests` writes the editor with a
sample on the kick.
