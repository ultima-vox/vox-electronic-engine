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
