# FX-04 nonlinear alias — antialiased Drive

**Draft / HOLD / NO MERGE.** Development slice on top of `8e32925`. No new IDs, no codec change, no oversampling and no latency change. Wavefold remains open and is measured, not claimed fixed.

> **Follow-up (2026-11-21):** the Wavefold open item below now has an implementation slice, [Wavefold alias — second-order ADAA](WAVEFOLD_ALIAS_20261121.md). It keeps this Drive change untouched and closes the point-sampled Wavefold path; listening, native and host gates remain open.

## What was measured first

A bin-centred sine was fed through a single slot. Because the input period divides the analysis window, every harmonic lands exactly on a DFT bin, so all remaining positive-side energy is content the nonlinearity created and folded below Nyquist. Raw rows: [nonlinear-alias](evidence/nonlinear-alias-20261119.txt).

Before the change, at 1.0 amount:

| Test tone | Drive, point-sampled `tanh` | Wavefold |
|---|---|---|
| 2.0 kHz | 0.15 % folded (−28.2 dB) | 12.3 % (−9.1 dB) |
| 5.0 kHz | 4.31 % (−13.7 dB) | 86.8 % (−0.6 dB) |
| 8.0 kHz | 13.03 % (−8.9 dB) | 90.4 % (−0.4 dB) |

The Drive figure matches the known complaint: high-frequency content into a hard saturator produces audible non-harmonic material. Wavefold is worse, and that is **not** addressed here — see the open item below.

## Change

The Drive slot keeps its exact linear part and routes only the saturating term through first-order antiderivative antialiasing:

- shape: `x + amount · (tanh(x · gain) · level − x)`, gain `1 + 7·amount`, level `1/√gain`;
- the saturating term is evaluated as the divided difference of its antiderivative `amount · level · log cosh(gain · x) / gain` between the previous and current input, using the closed form `log cosh(a) = |a| + log1p(e^(−2|a|)) − log 2` for stable evaluation;
- at `amount = 0` the nonlinear term is identically zero and the slot returns the input unchanged, so a bypassed Drive stays bit-exact;
- selecting a different slot type clears the ADAA history. A silent input feeds an identically silent input, so the reset is inaudible and a freshly selected slot is deterministic.

No allocation, no buffer, no added latency, no new state in the Program codec. The two history samples live in the existing per-slot DSP state.

## Result

Same probes, at 1.0 amount: **−13.7 → −19.6 dB** at 5 kHz and **−8.9 → −14.0 dB** at 8 kHz; the 2 kHz case improves from −28.2 to −34.1 dB. That is 5.1–5.7 dB less folded energy, and 4–10× less in linear terms, from a single first-order stage without oversampling.

Cost: the `FxEffectsBenchmark` 96 kHz "Shepard 8×4 + 4× Drive + 64 routes" case moved p50 `509.94 → 487.19 µs` and max `617.40 → 715.01 µs` in this container. Single-run Linux numbers on a shared machine are noise-level here, not a deadline claim; FX-T18 stays open.

## New executable check

`tests/suite/FxNonlinearAliasTests.cpp` (CTest `suite_fx_nonlinear_alias_tests`, label `suite`) measures, at 44.1/48/96 kHz and three tones, the folded energy of the antialiased Drive against a point-sampled reference of the **same** transfer function. It fails unless the antialiased slot folds at most 55% of the reference energy and stays below 6% absolute, and it checks that zero amount is alias-free. This catches a regression that silently drops back to point-sampled generation.

## Open, deliberately not weakened

Wavefold still generates point-sampled. Its antiderivative is not a smooth closed form (the clamp plus sine-fold shape has a discontinuous derivative at the clamp), and a first-order ADAA prototype made the 2 kHz case **worse** (−9.1 → −5.4 dB), so it was rejected rather than shipped. Closing Wavefold needs either 2× oversampling with prepared polyphase stages or a redesigned fold shape whose antiderivative is real analytic; both change the sound and the CPU budget and need their own measurement and listening step.

**Not qualified:** listening acceptance of the slightly darker Drive, full harmonic spectra on musical material, oversampling/quality-mode design, Windows/native behaviour, and loaded scheduling.

## Verified at `8e32925`

- **50/50 Linux executables** pass at `8e32925daa997f45772bb96bd72d6044047b6fb1`: [47 standalone](evidence/nonlinear-alias-inventory-20261119.txt) including the new alias test, plus the [three pinned-JUCE native](evidence/nonlinear-alias-native-20261119.txt) processor/runtime-stress/export harnesses.
- [ASan/UBSan](evidence/nonlinear-alias-sanitizers-20261119.txt) passed eight executables including `FxNonlinearAliasTests`.
- All frozen pins still match: nine legacy Granular float streams, nine legacy Glitch WAVs, and every fixed Delay render.
- Of the 360 review-candidate metric rows, 296 are byte-identical to the pre-ADAA run; the remaining 64 are exactly the candidates that carry a Drive slot. None crosses a quality limit (max peak `0.051026`, min mono/stereo RMS `0.84005`, max DC `0.0002907`, max 5 ms edge `0.000195`).
