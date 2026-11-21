# FX-04 Wavefold: diagnostic comparison

**Diagnostic only / HOLD.** This branch starts at `next/fx-development@c7ee608a0879087d9dade1f2471c69a1bde92732`. It changes no production DSP, state, IDs, latency, quality mode, golden fixture, or default sound. The experiments below are not candidate approval or a Windows/DAW result. TL follow-up: [7070341742](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7070341742).

## Baseline and frequency correction

The existing native `FxNonlinearAliasTests.cpp` was compiled and run unchanged. Its output matches the earlier reported 48 kHz amount-1 ratios of 0.1233, 0.8683 and 0.9041 at 2001, 5001 and 8001 Hz ([raw](evidence/wavefold-prior-screen-20261121.txt)). The old test uses the same FFT bins at every sample rate: its 96 kHz measurements are at 4002, 10002 and 16002 Hz, not 2/5/8 kHz. The new study independently chooses the nearest bin for each **absolute** target at each rate. It captures the current production `effects::Chain` after 0.5 s settling and verifies it against the point-transfer model to within 0.0002 absolute (float parameter smoothing settles just below one).

The table is the positive-frequency power **outside the in-band integer-harmonic bins**, divided by positive-frequency power. For a bin-centred single sinusoid this detects non-harmonic folded components, but is only a **lower bound**: aliased components colliding with a harmonic bin are counted as harmonic. DC and Nyquist bins are excluded. Do not equate it to complete alias energy or use it as an absolute perceptual score.

| Rate | Target | Production | Offline filtered 2x | Smooth sine ADAA alternative |
|---|---:|---:|---:|---:|
| 44.1 kHz | 2 / 5 / 8 kHz | 12.33 / 86.83 / 90.41% | 0.97 / 19.69 / 86.14% | 4.87 / 11.02 / 50.17% |
| 48 kHz | 2 / 5 / 8 kHz | 12.33 / 86.83 / 90.41% | 0.92 / 14.23 / 85.02% | 5.89 / 11.01 / 49.07% |
| 96 kHz | 2 / 5 / 8 kHz | 1.04 / 52.94 / 85.55% | 0.06 / 1.94 / 4.19% | ~0 / 24.14 / 21.45% |

The [raw spectral-summary CSV](evidence/wavefold-study-20261121.csv) includes exact bin-centred frequencies, output RMS/peak, model comparisons, offline wall/thread CPU times, and a 7/9/11 kHz three-tone stress proxy. The [top-32 spectral-bin CSV](evidence/wavefold-spectra-20261121.csv) records each case's strongest positive-frequency components and their power fractions; it is a truncated spectrum, not a complete energy inventory. The proxy is **not** a real multilayer voice or a quantified intermodulation-alias test. A 16x filtered offline reference is approximate, not a perfect bandlimited oracle; waveform error includes gain, timbre and phase differences. The sine alternative is a different transfer, with a half-sample ADAA history effect, not the rejected triangle ADAA prototype.

The 2x reference uses SciPy `resample_poly` with a Kaiser-10 filter in both directions and offline delay compensation; it is non-causal as measured. A causal streaming implementation would need independently designed interpolation/decimation, state, reset, latency and CPU validation. Linear up/down gain at the tested tones was approximately 0.99998–1.00001; this alone says nothing about nonlinear output timbre. At 48 kHz/5 kHz, production RMS was 0.542 and offline 2x RMS 0.212; at 96 kHz the three-tone 2x output peak reached 1.175. Thus lower measured off-harmonic energy can coincide with substantial level/timbre or headroom changes. No new sound should be adopted on this evidence.

The whole-buffer offline prototype timings in the CSV are medians of three runs: filtered 2x wall/thread CPU approximately 2.65–4.56 ms and smooth ADAA approximately 0.13–0.25 ms over a 0.5 s settling signal plus 16,384 samples. These Python/NumPy/SciPy figures compare neither native callback CPU nor 64-frame latency and cannot be used to approve a realtime implementation. Production timing is deliberately left blank rather than timing retrieval of a precomputed array.

## Native production stress screen

The [raw runtime CSV](evidence/wavefold-runtime-20261121.csv) records 1,440 individual blocks: actual header-only `NoiseRiser`, four enabled Shepard layers, max-ratchet Pattern at 200 BPM and four serial **current production** Wavefold slots at wet/amount 1; 96 kHz, 64 frames; 1/4/8 **initial** voices; three independently prepared 160-block trials. Pattern note-ons can change the active voice count after the initial block. The callback-equivalent region covers the scheduler, engine and effects, but not the full JUCE plugin, host routing, automation or allocation/lock probes; it is not FX-T18 acceptance. Preparation and capture are outside the timer. Wall and `CLOCK_THREAD_CPUTIME_ID` are recorded independently; no core pinning or background-load control was applied.

In this one Linux container run, there were 0/1,440 wall deadline misses and 0/1,440 thread-CPU deadline misses against 666.667 µs; peak left-channel amplitude ranged up to 0.99992. For the three 8-initial-voice trials, wall p50 was 274–276 µs and maximum 400–625 µs. These are diagnostic observations on a shared Linux VM, not a Windows deadline or audible-clipping certification. No best-trial selection was used. Allocations and locks were not measured by this standalone screen; the real plugin's FX-T18 checks remain open.

## Reproduction

Environment: Linux 6.18.57, AMD EPYC 9V74, three logical CPUs visible, g++ 14.2.0, Python 3, NumPy 2.3.5, SciPy 1.17.0. From the repository root:

```sh
g++ -std=c++20 -O3 -Iproducts/fx tests/suite/FxNonlinearAliasTests.cpp -o /tmp/fx-alias && /tmp/fx-alias
g++ -std=c++20 -O3 -Iproducts/fx tools/fx_wavefold_study/contracts.cpp -o /tmp/fx-contracts && /tmp/fx-contracts
python3 tools/fx_wavefold_study/study.py --spectra-csv /tmp/wavefold-spectra.csv > /tmp/wavefold-study.csv
python3 tools/fx_wavefold_study/verify.py
g++ -std=c++20 -O3 -Iproducts/fx tools/fx_wavefold_study/runtime.cpp -o /tmp/fx-runtime && /tmp/fx-runtime > /tmp/wavefold-runtime.csv
```

The native contract screen passed settled wet-zero, amount-zero and repeated fresh-prepare determinism. It does not replace pinned golden, reset-after-wrap, transition, plugin/state/automation or allocation/lock tests. Numbers will vary with hardware and load.

## Decision

Keep the existing sound and frozen fixtures. The offline 2x filter is promising at some frequencies but leaves significant folded energy at 8 kHz/44.1–48 kHz and changes level/headroom; the smooth transfer changes the sound substantially. Neither is a validated realtime candidate, so no silent latency, state or timbre change is justified. Next compare a causal polyphase design and a better-filtered/higher-rate alternative under matched output/loudness, examine actual multilayer program spectra, run native four-slot allocation/lock checks and FX-T18 1/4/8 + Pattern with separate thread CPU/wall on controlled Windows, and obtain explicit TL and listening review before any sound/latency change. Existing FX-T18, native OLE/Cubase/REAPER and owner approval remain open.
