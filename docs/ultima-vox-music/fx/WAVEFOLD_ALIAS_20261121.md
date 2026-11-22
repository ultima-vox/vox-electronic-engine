# FX-04 Wavefold alias — second-order antiderivative antialiasing

**Draft / HOLD / NO MERGE.** Development slice on top of
`c7ee608a0879087d9dade1f2471c69a1bde92732` (branch
`arena/a7cf1349-vox-electronic-engine`). The static Wavefold transfer function
is unchanged, no ID/codec/state/parameter changes, no oversampling, no
allocated buffers, no integer latency and no PDC change. The existing Drive
ADAA path is bit-identical and its test limits are untouched.

> **2026-11-22 update — ADAA2 is rejected for integration.** The TL decision is
> to keep the original Wavefold and test first-order ADAA next
> ([experiment](WAVEFOLD_ADAA1_20261122.md)). This document is retained as the
> rejected prototype's evidence. A post-review frequency-response
> study ([evidence](evidence/wavefold-response-20261122.txt),
> [CSV](evidence/wavefold-response-20261122.csv)) shows the method's linear
> response is a 3-point average with a **null at fs/3** (14.7 kHz at 44.1 kHz)
> and a 180° phase flip above it. The alias and CPU measurements below remain
> valid, but the shipping decision is withdrawn pending an oversampling or
> compensation choice. See [Frequency response](#frequency-response--the-3-point-average-2026-11-22).

## What was measured first

The existing `FxNonlinearAliasTests` feeds a bin-centred sine through one slot.
Every harmonic lands on a DFT bin, so all remaining energy is content the
nonlinearity folded below Nyquist. At 44.1 kHz, amount 1.0, before this change:

| Tone | Wavefold point-sampled | Ratio | Absolute folded power |
|---|---|---|---|
| 1.84 kHz | 12.3 % | −9.1 dB | −11.4 dB |
| 4.59 kHz | 86.8 % | −0.6 dB | −2.9 dB |
| 7.35 kHz | 90.4 % | −0.4 dB | −2.7 dB |

At the shipped preset amounts (0.28–0.5) the 4.6 kHz case already folds 12–87 %
of the output power. This is the reported FX-04 alias gap: the old slot called
`asin(sin())` point-sampled, i.e. it sampled a triangle wave with an unbounded
harmonic series.

## Options compared

All candidates keep the same triangle fold. Every number below is **absolute
folded (non-harmonic) power relative to a unit-amplitude sine**, in dB; lower is
better. `point` is the arithmetic triangle (the old code's shape), `ADAA1` and
`ADAA2` are first/second-order antiderivative antialiasing, `2×OS`/`4×OS` are
oversampled point-sampled folds, and the `+ADAA2` rows run ADAA at the
oversampled rate. Raw rows: [wavefold-options](evidence/wavefold-options-20261121.txt).

| Tone | Amount | point | ADAA1 | **ADAA2** | 2×OS | 4×OS | 2×OS+ADAA2 | 4×OS+ADAA2 |
|---|---|---|---|---|---|---|---|---|
| 1.84 kHz | 0.28 | −24.9 | −31.2 | **−36.7** | −37.1 | −39.0 | −41.9 | −40.2 |
| 1.84 kHz | 0.50 | −22.3 | −32.8 | **−39.7** | −35.7 | −43.9 | −48.9 | −47.4 |
| 1.84 kHz | 1.00 | −11.4 | −17.1 | **−22.8** | −22.9 | −24.3 | −26.9 | −25.0 |
| 4.59 kHz | 0.28 | −9.9 | −15.5 | **−22.2** | −18.8 | −19.2 | −21.6 | −19.8 |
| 4.59 kHz | 0.50 | −3.4 | −9.0 | **−14.2** | −13.7 | −14.2 | −15.8 | −14.6 |
| 4.59 kHz | 1.00 | −2.9 | −16.5 | **−18.5** | −19.1 | −24.0 | −41.7 | −33.2 |
| 7.35 kHz | 0.28 | −2.2 | −7.7 | **−16.3** | −8.9 | −9.0 | −11.2 | −9.5 |
| 7.35 kHz | 0.50 | −2.7 | −17.3 | **−16.3** | −16.6 | −17.4 | −29.4 | −19.4 |
| 7.35 kHz | 1.00 | −2.7 | −9.9 | **−21.4** | −5.0 | −21.7 | −17.1 | −20.2 |

Cost and timing of the same candidates (this container, one channel, no chain
overhead; 128-sample stereo blocks):

| Option | CPU ns/sample/channel | Group delay | Notes |
|---|---|---|---|
| point (arithmetic) | ~4.3 | 0 | no state |
| ADAA1 | ~9.7 | 0.5 sample (method) | 2 doubles/channel |
| **ADAA2 (shipped)** | **~11.6** | **1.000 sample, measured** | 5 doubles/channel, no buffer |
| 2×OS | ~70–85 | 15 input samples (31-tap) | filter state + PDC |
| 4×OS | ~200–225 | 23 input samples (31-tap) | filter state + PDC |
| 2×OS+ADAA2 | ~83–89 | 15 input samples (31-tap) | filter + ADAA state |
| 4×OS+ADAA2 | ~230–265 | 23 input samples (31-tap) | filter + ADAA state |

The oversampling rows are prototype-only measurements with a **centred**
linear-phase halfband, so their quality is an upper bound that does not pay the
15/23-sample real-time delay. Even with that advantage they do not win
uniformly: at 7.35 kHz/0.28, 2×OS+ADAA2 (−11.2 dB) is 5 dB worse than ADAA2
(−16.3 dB), while costing ~7× the CPU and needing host latency compensation.
Only 2×OS+ADAA2 at 4.59 kHz/1.0 (−41.7 dB) is dramatically better than ADAA2,
and it needs PDC plus prepared filters to be usable.

**Decision on alias and CPU: second-order ADAA on the unchanged static shape.**
It removes 10.8–18.7 dB of absolute folded power across all nine probes, needs
no oversampling, allocation, buffer or integer latency, and costs about 2.7× the
arithmetic triangle (still under the shipped chain cost, see below). Oversampling
was rejected for this slice because the benefit is not uniform, the CPU is
6–20×, and the plugin currently reports no latency compensation.

**This is not a shipping decision.** The later frequency-response measurement
below found an fs/3 null in the ADAA2 passband that the alias probes do not
cover. ADAA2 is therefore an experimental diagnostic until that is resolved.

## Change

`products/fx/engine/Effects.h` keeps the exact static shape and replaces only
the evaluation:

```text
u = clamp(gain * x, -8, 8),  y = tri(u),  gain = 1 + 7 * amount
```

`tri` is the 4-periodic triangle: `u` on [−1, 1], +1/−1 at odd integers, 0 at
even integers, 0 in the clamped region beyond ±8. Its first and second
antiderivatives are exact piecewise polynomials (`A`, `B` below), which means
no approximation table, no oversampling and no special functions:

```text
for u in [2m, 2m+2), s = |u| − 2m:
  A(s) = s²/2                  (s ≤ 1), 2s − s²/2 − 1   (s > 1)
  B(s) = s³/6                  (s ≤ 1), s² − s³/6 − s + 1/3 (s > 1)
  F1(u) = (−1)^m A(s) + S(m)                       (even)
  F2(u) = (−1)^m B(s) + S(m)·s + m                 (odd)
  S(m) = 1 for odd m, else 0
```

The slot then applies the second-order ADAA divided difference

```text
y[n] = 2/(x[n]−x[n−2]) · ( D[n] − D[n−1] )
D[n] = (F2(x[n]) − F2(x[n−1])) / (x[n] − x[n−1])
```

with the published mean-value fallbacks when consecutive inputs coincide. The
implementation caches `F2(x[n−1])` and the previous divided difference, so a
steady slot pays one `F2` evaluation per sample; it recomputes the cache only
when the smoothed amount actually changes the gain.

Deliberate properties:

- **No new state in the Program codec.** Per channel the slot keeps five
  doubles of DSP history. `set()` clears it on a type change, exactly as the
  Drive history is cleared, so a freshly selected slot is deterministic.
- **No allocation, no locks, no I/O** on the audio path; the helpers are
  arithmetic and `std::floor` only.
- **Non-finite input is rejected** before the floor/parity mapping and clears
  the history, so a bad sample cannot poison later output or reach an integer
  cast.
- **Drive is untouched.** Its branch, thresholds and history are unchanged;
  the benchmark's four-Drive checksum is byte-identical before and after.

## Result

Alias, measured by the extended test at all rates (44.1/48/96 kHz) and amounts
0.28/0.5/1.0: absolute folded power improves by **10.8–18.7 dB** at the nine
44.1 kHz probes, with a worst residual of **−14.2 dB** (4.59 kHz, amount 0.5).
The old ratio at that probe was 86.5 %; it is now 31.8 %. At 7.35 kHz/0.5 the
ratio falls from 100 % to 15.3 % while absolute folded power falls 13.6 dB.
Raw rows: [wavefold-alias](evidence/wavefold-alias-20261121.txt).

CPU, `FxEffectsBenchmark` focused slot-only section (four serial slots, 96 kHz,
128-sample stereo blocks, 3 runs each; raw: [wavefold-cpu](evidence/wavefold-cpu-20261121.txt)):

| Workload | Before p50 | After p50 | Change |
|---|---|---|---|
| 4× Wavefold, amount 0.5 | 46.14 µs | 28.93 µs | −37.3 % |
| 4× Wavefold, amount 1.0 | 48.33 µs | 30.29 µs | −37.3 % |
| 4× Drive, amount 0.5 | 38.25 µs | 38.63 µs | noise (same checksum) |
| 4× Drive, amount 1.0 | 37.04 µs | 37.20 µs | noise (same checksum) |

Per sample/channel/slot that is **45.1 → 28.3 ns** at amount 0.5. The slot
became cheaper because the old expression called `acos(-1.0)` twice per sample
in addition to `asin`/`sin`; the ADAA2 path is pure arithmetic. The whole-engine
Shepard row moved from ~581 to ~563 µs p50, inside this container's noise.

Latency: the slot's measured low-frequency group delay is **1.000 input
sample** at 44.1/48/96 kHz (linear region), the p/2 delay inherent to
second-order ADAA. That is only the low-frequency limit: the same measurement
shows the amplitude response is a 3-point average with an fs/3 null, not a pure
delay (see §Frequency response).
There is no integer pre-delay, no buffer and no PDC change. For comparison the
Drive slot measures **0.198 sample** at amount 0.28 because its ADAA wet term
is mixed with an undelayed dry term. The 1-sample Wavefold shift is 20.8 µs at
48 kHz and must be considered if a host session uses a parallel dry copy; it is
not a plugin latency report. Raw: [wavefold-options](evidence/wavefold-options-20261121.txt)
and `evidence/measure-wavefold-latency.cpp`.

## Frequency response — the 3-point average (2026-11-22)

For a linear segment the two-stage divided difference reduces exactly to

```text
y[n] = (x[n] + x[n-1] + x[n-2]) / 3
```

so the method is not a pure one-sample delay: its response is the 3-point
average `S(ω) = (1 + e^-jω + e^-j2ω)/3`. That response has a **zero at
ω = 2π/3, i.e. f = rate/3**, and a 180° phase inversion above it. A low-level
bin-centred sine through the real `Chain` (762 points; measured against theory
to 0.023 dB / 0.009°) gives, for one wet-1 slot at amount 1, candidate minus
point-sampled parent:

| rate | 1 kHz | 5 kHz | 10 kHz | near fs/3 | 20 kHz |
|---|---|---|---|---|---|
| 44.1 kHz | −0.06 dB | −1.56 dB | −7.14 dB | grid −38.1 dB | −10.8 dB |
| 48 kHz | −0.05 dB | −1.31 dB | −5.78 dB | grid −22.8 dB | −13.2 dB |
| 96 kHz | −0.01 dB | −0.32 dB | −1.26 dB | grid −25.7 dB | −5.5 dB |

An exactly-fs/3 tone is removed in the small-signal limit (the linear null is
phase-independent): 0.5-amplitude, phase-0 sines at 14.7/16/32 kHz leave peaks of
8.5e-12/1.0e-11/2.1e-11, and at 44.1 kHz the phase-0 result stays ~1e-12 from
amplitude 0.001 to 0.9. Large signals at other phases are not exactly zero but
remain deeply notched. Four
serial slots compound the average to `S^4` (−28.6 dB at 10 kHz for 44.1 kHz).
Wet 0.5 only partially fills the null (worst grid point −18.6 dB).

The absolute alias figures above are measured output alias energy and remain
valid, but they should not be read as flat-response alias suppression: at the
three probe frequencies the fundamental is itself attenuated by up to 3.5 dB,
and the folded/output ratio improves by 2.1–15.7 dB, less than the absolute
figures. The passband null is the larger issue.

Implications for the options already measured: moving ADAA2 to 2× oversampling
moves the null to 2fs/3 (29.4 kHz at 44.1 kHz) but still droops about 7 dB at
20 kHz; 4× moves it to 4fs/3 with roughly 1.5 dB at 20 kHz, at the measured
23-input-sample halfband delay, 230–265 ns/sample and required PDC. First-order
ADAA has no mid-band null, but its 2-point average rolls off toward Nyquist
(about −6 dB at 15 kHz/44.1 kHz) and its alias suppression is 5–7 dB weaker.
The exact inverse of the 3-point average has poles on the unit circle and cannot
be used directly, so compensation would be a reviewed design, not a patch.
None of these alternatives is implemented in this slice.

## Compatibility

- FXSL type id **3** is unchanged; 6-bit wet/amount packing is unchanged; the
  FXSL v1 3/6/7-bit legacy decode is unchanged. The test asserts both round
  trips and a wavefold preset-shaped value.
- No new parameters, no ID changes, no Program/FXSL schema change, no state
  field. Existing sessions and presets load exactly as before.
- The static transfer function is unchanged. The test drives DC values and a
  quasi-static −2…+2 sweep through the slot and compares them with the literal
  old `asin(sin())` expression (tolerance 1e-6 DC, 2e-3 sweep).
- The audio of a Wavefold slot necessarily changes: that is the alias removal.
  Presets that carry Wavefold will have different candidate renders and must be
  re-measured when the review corpus is re-rendered; this slice does not claim
  the existing 360-row metric snapshot is unchanged. Drive-only renders are
  unaffected (benchmark checksums identical).

## New executable checks

`tests/suite/FxNonlinearAliasTests.cpp` now:

1. keeps the existing Drive ADAA limits (≤55 % of the point-sampled reference
   and <6 % absolute) and the zero-amount check;
2. measures Wavefold against an **independent** point-sampled reference built
   from the old literal `asin(sin())` expression, at 3 rates × 3 tones ×
   3 amounts; fails unless absolute folded power is ≤20 % of that reference and
   ≤10 % of the input power;
3. checks DC transfer against the old shape on both channels, and a
   quasi-static −2…+2 sweep;
4. checks group delay is 0.9–1.1 samples for Wavefold and <0.5 for Drive;
5. checks silence stays exactly silent and the type/packing/legacy codec.

Compiled against the base commit's `Effects.h`, this test fails **45 checks**,
so a silent fall back to point sampling cannot pass.

## Verified in this container

Linux, GCC 12.2.0, `-O2`, Intel Xeon 2.60 GHz. Full inventory:
[wavefold-inventory](evidence/wavefold-inventory-20261121.txt).

- 8/8 standalone executables pass with 0 failures: `FxEffectsTests`,
  `FxNonlinearAliasTests`, `FxOfflineRenderTests`, `FxEffectTailTests`,
  `FxBarEventTests`, `FxSoundGraphTests`, `FxPortablePresetTests`,
  `FxFactoryCandidatesTests`.
- ASan/UBSan pass for `FxNonlinearAliasTests`, `FxEffectsTests` and
  `FxEffectTailTests`.
- The new test fails 45× against the base `Effects.h`; it passes against this
  change.
- No CMake/CTest or JUCE toolchain is installed in this container, so this is
  a direct local build inventory, not the CTest suite. Windows, the native
  processor, VST3 validator, binary SHA and Cubase/REAPER hosting are **not**
  verified here. FX-T18 remains open.

## Open, deliberately not weakened

- Listening acceptance is pending, and the fs/3 null above is a sound blocker,
  not a nuance: a 14.7 kHz tone disappears at wet 1. Musical A/B is still
  required, but the passband response must be fixed before this can be judged
  as an alias improvement.
- The 4.59 kHz/0.5 residual is −14.2 dB. It is 10.8 dB better than before but
  not inaudible. Closing it further needs 2× oversampling **with PDC** or a
  higher-order ADAA; both are separate measured decisions.
- 2×/4× oversampling and combined ADAA2+OS were measured but not shipped; no
  prepared polyphase stages, no latency reporting, no buffers were added.
- Review-candidate metrics carrying Wavefold must be regenerated; no quality
  limit or preset acceptance is claimed for them in this slice.
- The owner-approved create-only Save, private delivery, Windows/native and
  host qualification remain outside this change.
