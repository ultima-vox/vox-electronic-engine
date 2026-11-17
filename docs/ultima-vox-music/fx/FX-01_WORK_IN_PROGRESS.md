# FX-01: interim instrument foundation (not a release)

The standalone instrument's identity, stereo-only output and MIDI input are
unchanged. This checkpoint is incomplete: only a four-second one-shot Noise
Riser and output gain exist. No four-layer Program or full product UI exists.

## Interim behavior

* `fx.output.gain` is a normalized linear host parameter, default 1.0,
  smoothed over 5 ms. Its stable ID and range are in `products/fx/parameters.csv`.
* FPRM is an FX product section within VOXS schema 1. It contains a little
  endian 32-bit version (1), then an IEEE-754 32-bit normalized gain. Missing
  FPRM imports FOUND-01 shell states at the default; malformed, future-version
  or non-finite/out-of-range values are rejected before any state commits.
* NoteOn is sample-accurate and starts a one-shot; NoteOff and velocity-zero
  NoteOn do not cut it off in this interim mode. CC120 and CC123 fade events
  on the addressed MIDI channel over 5 ms. Host reset fades all channels.
  This policy is not the final configurable trigger-mode contract.
* Up to eight active events and one 5 ms retiring event are preallocated. A
  ninth event retires the oldest; a subsequent trigger while retirement is
  occupied is dropped. This is a bounded interim policy, not configurable
  stealing. The four-second one-shot has a 20 ms end fade and no effects tail.
* The prepared coefficient table linearly interpolates 4097 points over the
  event. Its source/reference is the preceding checkpoint's per-sample
  exponential implementation, preserved as a test-only reference. Render
  overwrites both output buffers and requires valid pointers for the span.

## Local observations, not qualification

On Linux, Intel Xeon 2.60 GHz, GCC 12.2.0, `-O2`, 300 consecutive tests of
128-sample DSP renders, preparation and trigger excluded. These are wall-clock
microbenchmarks, not plugin callback timing, CPU-load measurements or Windows
results. Nine voices means eight active plus one retiring (first 128 samples).

| Sample rate | Voices | p50 us | p95 us | p99 us | max us |
|---:|---:|---:|---:|---:|---:|
| 44100 | 1 | 0.78 | 1.41 | 1.79 | 2.52 |
| 44100 | 8 | 3.95 | 6.07 | 14.39 | 45.32 |
| 44100 | 9 | 4.42 | 7.36 | 12.79 | 26.57 |
| 48000 | 1 | 0.83 | 1.38 | 1.75 | 4.62 |
| 48000 | 8 | 3.83 | 6.24 | 7.61 | 24.87 |
| 48000 | 9 | 4.27 | 4.42 | 11.91 | 27.26 |
| 96000 | 1 | 0.74 | 0.92 | 1.01 | 1.15 |
| 96000 | 8 | 3.71 | 3.74 | 7.96 | 19.96 |
| 96000 | 9 | 4.16 | 4.19 | 9.70 | 19.26 |

Across each full four-second single-event reference render at the three rates,
maximum absolute sample error was 3.73e-9 and maximum relative RMS error
5.16e-8. The test thresholds are 1e-6 and 1e-5 respectively. An additional
first-difference energy test checks that later energy rises; it is not a
complete spectral/aliasing quality assessment. The DSP test was also run
with AddressSanitizer and UndefinedBehaviorSanitizer. Windows performance,
actual plugin callback distributions, host validation and allocation/locking
proof await exact-head CI and further independent analysis.

## Subsequent development

An interim Perform panel binds the real output parameter using JUCE's slider
parameter attachment and provides coalesced audition/panic requests and a
meter sourced from the rendered block peak. It is not the full product UI.
The JUCE-free timeline primitives now provide validated 2–16 node curves,
seconds and tempo-integrated bars clocks, and per-layer time mapping. Only
the seconds clock is currently used by the Noise Riser; bars, layer mapping
and curve editing are not yet connected to processor state or the UI. The
DSP API can snapshot 0.05–32 second durations per trigger; the processor still
uses its fixed four-second default. No claim of complete FX-02 is made.

## Layered noise engine checkpoint

The JUCE-free Noise Riser engine can snapshot four independently enabled
noise layers per event. Each layer has its own normalized event offset/span,
gain, pan, gain curve and pan curve; enabled layers are averaged per event to
keep the existing output bound. Eight event slots plus one retirement slot
remain fixed-size. The UI/host cannot yet edit, publish or serialize these
layer settings; the plugin uses the original single-layer default. Pitch,
cutoff curves, other sound families and a realtime-safe Program publication
bridge are still absent. `setProgram` is only safe with exclusive engine
access and must not be called directly by a concurrent UI thread.

After adding layers, a repeat of the Linux DSP-only 128-sample benchmark
(same compiler/host and method as above; one active layer per event) produced:

| Sample rate | Voices | p50 us | p95 us | p99 us | max us |
|---:|---:|---:|---:|---:|
| 44100 | 1 | 4.51 | 4.70 | 6.49 | 35.94 |
| 44100 | 8 | 16.64 | 25.56 | 30.94 | 40.35 |
| 44100 | 9 | 18.71 | 28.47 | 51.45 | 62.73 |
| 48000 | 1 | 4.66 | 5.18 | 5.33 | 22.27 |
| 48000 | 8 | 23.18 | 29.38 | 60.44 | 115.32 |
| 48000 | 9 | 21.61 | 30.23 | 46.97 | 51.73 |
| 96000 | 1 | 4.54 | 5.99 | 23.78 | 47.09 |
| 96000 | 8 | 26.64 | 32.90 | 66.72 | 70.97 |
| 96000 | 9 | 19.27 | 29.24 | 44.97 | 53.03 |

These results supersede the earlier pre-layer benchmark, but are not a
four-layer stress benchmark, a real host callback distribution, or Windows
performance evidence. Optimisation and worst-case measurement remain required.

## Duration parameter and state v2

`fx.event.duration` is a host-automatable 0.05–32 s value. The processor
snapshots its value once per block for newly triggered events; changing it does
not alter events already playing. The Perform panel binds it via a JUCE slider
parameter attachment. The reported tail is 32 seconds. FPRM v2 appends the
normalized duration after the normalized output gain; v1 and the original
shell's absent FPRM import with the default 4 seconds. State restores still
use the SuiteProcessor transactional product-section protocol. FX-02's
bar-based timing and transport behavior are not connected yet.

## Program bridge foundation

A fixed three-slot, single-producer/single-consumer Program bridge validates
before publication, reports a full queue, and copies only the newest claimed
snapshot into caller-provided storage on the consumer. Late observations of
older snapshots are discarded. It uses lock-free 32-bit atomic slot ownership;
there is no audio-thread allocation or object graph retirement. Local stress
and ThreadSanitizer tests exercise snapshot consistency. The bridge is not
wired to the plugin or host state yet: doing so requires a controller that
serializes all edits and coordinates transactional restore without losing
publication when the bounded queue is full. It must not be called concurrently
by multiple producers.

## Transport discontinuity policy (interim)

When a host supplies playing state and absolute sample positions, a transition
from playing to stopped or a sample-position discontinuity while playing
fades currently active events over 5 ms at the next block boundary. A note or
editor audition in that block can still start a new event. Missing playhead
information clears continuity history instead of assuming a stop/seek. Tests
using a simulated JUCE playhead were added but require the processor test to
run in the suite build; Cubase/REAPER behavior has not been verified.

## Downlifter engine foundation

A second layer recipe combines a descending MIDI-transposed sine sweep and a
falling noise low-pass/envelope. Frequency and filter trajectories are prepared
outside the callback. Local renders at the three target sample rates and block
sizes pass finiteness, audibility, broad brightness trajectory and short-window
fundamental-energy checks, including ASan/UBSan. This is not yet available in
plugin Program state or the UI; audible review, alias/CPU qualification and
full FX-03 acceptance remain outstanding.

## Interim family selector

The Perform panel now selects Noise Riser or Downlifter as a structural choice,
not a host-automatable parameter. An atomic integer is read at block boundaries;
the audio thread creates the bounded layer settings and existing events keep
the family they started with. The `FFAM` product section stores version 1 and
the validated family ID as two little-endian u32 values. States missing FFAM
migrate to Noise Riser; unsupported versions and IDs are rejected. This is a
single-scalar handoff, not the full layered Program controller. Processor-level
family, state and editor tests have been added, but have not yet run in a full
JUCE build or VST3 host.

## Shepard sweep

A six-partial octave-overlap tonal sweep is selectable as FFAM family 2.
Per-partial Hann-like amplitude windows suppress octave-wrap discontinuities;
partials fade before Nyquist. Local DSP tests render complete two-second
sweeps at 44.1/48/96 kHz, compare 64/512-sample blocks, check finite bounded
sound, and compare against the noise riser. ASan/UBSan passed locally. This
is an initial implementation: perceptual octave continuity, alias rejection,
worst-case CPU and processor/host behavior still require qualification.

## Impact / Boom

FFAM family 3 adds a descending sine body, separate sub and short noise
exciter with prepared exponential trajectories and a 1 ms attack fade. The
local DSP test covers complete four-second events at 44.1/48/96 kHz and
64/128/512 blocks; audibility, finite output and decaying tail pass, as does
ASan/UBSan. This does not establish perceptual quality, aliasing, maximum-load
performance or plugin-host validation.

## Whoosh / Doppler

FFAM family 4 adds a filtered noise/tonal pass with a fixed 256-sample per-layer
own-signal delay, interpolated moving read tap, an amplitude window and stereo
pan motion. Delay is bounded to 254 samples (at most 2 ms where the sample
rate permits). Tests at 44.1/48/96 kHz and 64/128/512 blocks check audibility,
finite output and left-to-right energy movement; ASan/UBSan passed locally.
The delay increases per-event memory and note-on initialization; real host
callback and maximum-load performance remain unmeasured. Listening and full
Doppler/alias qualification remain outstanding.

## Zap / Laser

FFAM family 5 adds an exponential MIDI-transposed sine chirp with a short
noise tick and rate-independent precomputed pitch/amplitude trajectories.
Tests cover 44.1/48/96 kHz, 64/128/512 blocks, output bounds, decay and a
normalized high-frequency trajectory proxy; ASan/UBSan passed locally. The
current recipe does not include optional FM, drive or oversampling; listening,
anti-alias measurement and exact-head host tests remain required.

## Psy Blip / Squelch

FFAM family 6 uses a pitch-falling sine/noise exciter and a moving resonant TPT
low-pass with precomputed coefficients. Local 44.1/48/96 kHz and 64/128/512
sample tests cover finite output, decay and distinction from Laser. An
additional eight-event/four-layer stress render at 96 kHz stayed finite and
below full scale; ASan/UBSan passed locally. Comb, ratchet, perceptual
acceptance, full filter inventory and exact-head VST3/host evidence remain.

## Metallic / Resonant

FFAM family 7 implements eight prepared, damped inharmonic modes with bounded
feedback coefficients and omission above 0.45 times sample rate. Local tests
cover full events at 44.1/48/96 kHz and 64/128/512 blocks, decay and finite
output, plus eight-event/four-layer stress at 96 kHz; ASan/UBSan passed.
This is an initial mode bank, not the full filter/controls inventory or a
perceptual/alias/performance qualification.

## Texture / Drone

FFAM family 8 uses two continuously phased detuned sine oscillators, filtered
noise and slow level/pan motion. Local 44.1/48/96 kHz, 64/128/512 block
renders demonstrate sustained output, stereo movement and finite bounds;
ASan/UBSan passed. No unison control, full VA waveforms or product-level
quality/host/performance qualification is claimed.

## Formant / Alien

FFAM family 9 uses three moving, prepared-coefficient TPT band-pass formants
excited by sine and noise. Local tests cover 44.1/48/96 kHz, 64/128/512
blocks, finite output, distinction from Texture and 96 kHz eight-event,
four-layer stress; ASan/UBSan passed. Full vowel control, modulation routing,
perceptual/alias qualification and host verification remain outstanding.

## Layered Program publication and persistence

The processor now persists the current four-layer Program in a versioned
`FPRG` section (explicit little-endian fields and bounded curves). Legacy
states without FPRG migrate from FFAM; mismatched FFAM/FPRG or malformed,
future and invalid Program data are rejected before commit. Audio reads use a
single bounded attempt against fixed lock-free atomic words: a concurrent
writer causes the old snapshot to remain active until a later block. Writers
serialize on non-audio threads; no resource graph is retired in the callback.
The old three-slot bridge remains a standalone experimental component and is
not used by the processor. Controller APIs expose Program updates, but a full
layer/curve editing UI is not yet implemented.

Standalone codec tests include truncation, random inputs, unsupported version
and transactional failure. Program-store concurrent tests passed under TSan;
Linux allocation probe measured zero allocations for a changed snapshot read.
JUCE processor integration tests for four-layer audio and project restore were
added and syntax-checked only; they await a runnable suite build and host tests.

## Basic layer controls

The interim panel exposes enable, family, gain, pan, start and length for each
of the four layers. These are structural Program edits for newly triggered
events, not host automation; layer gain/pan automation and editable curves
remain required. Layer edits use serialized non-audio read-modify-write so
concurrent changes to different layers are not lost. JUCE editor binding tests
were added and syntax-checked but have not yet executed in a full suite build.

## Basic gain-curve editing

Each layer card has an editable gain curve: double-click inserts a node,
interior nodes drag with 1/16 time snapping, Delete removes an interior node,
and L/S/E/H set the outgoing shape. Ctrl-Z/Y undo and redo structural edits;
project restore clears the editor undo history. Curve values publish through
the Program store and are captured by newly triggered events. Tests for the
editor-to-Program binding and undo/redo were added but only syntax-checked.
The full Timeline workspace, selection/zoom, pan/pitch/filter/global curves,
accessible alternative node entry and Windows visual/DPI qualification remain.

## Modulation primitives

A JUCE-independent, bounded 64-route typed matrix, LFO and seeded
sample-and-hold have been added. The matrix applies signed offsets to a copy
of normalized host base values and prepares route smoothing off the callback.
Standalone tests cover validation, clamping, smoothing, reproducibility and
zero allocations in a Linux audio-path probe; ASan/UBSan passed. These helpers
are not wired into Program state, processor DSP or UI. Other sources, macro
assignments, route editing and complete FX-04 behavior remain outstanding.

## Windows stack safety in tests

The prepared engine currently occupies approximately 549 KB (local Linux
`sizeof`, including coefficient tables). Tests that create multiple engines or
processors now allocate them on the heap rather than relying on the default
Windows thread stack. This is an unverified Windows safeguard; exact-head
Windows execution remains pending. Engine memory and initialization costs
still need product-level profiling and optimization.

## Glitch / Stutter

FFAM family 10 captures the layer's own synthesized signal in its bounded
256-sample buffer, then loops windowed slices with deterministic gated
retriggers. The interim grid is fixed at 120 BPM; it is not host-synchronized
and has no sample-import/slice editor yet. Local tests at 44.1/48/96 kHz and
64/128/512 blocks check audibility, deterministic gate silence and finite
bounds; ASan/UBSan passed. Listening, artifact, transport-sync and VST3 host
qualification remain outstanding.

## Granular Cloud

FFAM family 11 uses an explicitly built-in, procedurally synthesized source
prepared outside the audio callback. A single preallocated pool contains at
most 64 Hann-windowed grains globally; note stealing remaps ownership and
panic retires grains. Local tests at 44.1/48/96 kHz verify audibility,
block-invariant deterministic output, the 64-grain cap and retirement. A
Linux allocation probe observed zero allocations during granular processing
and event stealing; ASan/UBSan passed. This is not full granular acceptance:
user sample import, safe sample ownership/retirement, reverse/position/rate
controls, sample portability, tuning and host/quality/performance evidence are
still required. The built-in source is not a hidden fallback for a missing
user sample; user sample loading is not yet offered.

## Generator foundation

A JUCE-independent seeded generator produces layered Programs with category,
style, complexity, intensity, energy, stereo movement and seconds; it supports
per-layer/per-field locks, mutation and distinct A/B seeds. A deterministic
48-entry procedural seed corpus covers four variants per currently implemented
family. Local tests verify determinism, lock preservation, structural uniqueness
and an audible finite smoke render of each entry; ASan/UBSan passed. These are
not yet curated/approved factory presets: the generator, corpus and locks are
not connected to plugin/UI/state, and key/pitch, patterns, full musical controls,
exports and perceptual/licensing qualification remain outstanding.

## Trigger-pattern and MIDI-export foundation

A JUCE-free offline scheduler supports one to four 4/4 bars of 1/16 steps,
seeded probability, one to four ratchets, trigger notes and velocities. It
exports a type-0 Standard MIDI File from the same canonical tick events; tests
parse the file and compare notes, note-offs and offsets, check bounds,
determinism and 12 structurally distinct seed patterns. ASan/UBSan passed.
This is not yet the internal realtime sequencer or a product export feature:
host synchronization, other grids/triplets, swing, UI, state, file delivery and
musical validation remain. MIDI represents triggers only, not synthesized audio.
