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
