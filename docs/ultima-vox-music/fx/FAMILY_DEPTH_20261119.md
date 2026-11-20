# FX-03 family depth — formant excitation and squelch resonance

**Draft / HOLD / NO MERGE.** Development slice on top of `833834e`; no workflow, ID, codec or content changes. This closes two measured defects in the existing twelve families and adds an executable distinctness screen. It does not claim family listening acceptance.

## Measured defects

A keyboard sweep of the twelve families (MIDI 24…127, 48 kHz, one layer, defaults) showed two families whose level collapsed in ordinary playing ranges while every other family stayed within 0.2–5 dB:

| Family | Before | Cause |
|---|---|---|
| Formant | **16.6 dB** spread; C4 −52.5 dBFS, C1 −59.2 dBFS, C5 −43.1 dBFS | The excitation was a single sine at the layer's tuned pitch. A vowel band only lit up when one of that sine's harmonics happened to land in it, so level and vowel identity jumped between notes. |
| Squelch | **22.8 dB** spread; C4 −48.6 dBFS, note 127 −70.3 dBFS (energy gone by 250 ms) | The 8 ms tone/noise burst was the only excitation; the 300 ms resonant sweep had nothing left to resonate. Above roughly note 119 the uncapped tone also ran into the filter's top octave. |

## Change

- **Formant**: a polyBLEP band-limited saw plus noise replaces the lone sine (`0.30 · saw + 0.70 · noise`). The BLEP edge keeps the excitation band-limited before the resonators, so no new alias energy is introduced, and the harmonic series excites all three vowel bands at every note. Resonator damping is 0.25 (was 0.32) so the formants read as peaks rather than a slope.
- **Squelch**: the tonal exciter is capped at `min(0.40 · rate, 6 kHz)` for alias safety, and a short noise skirt (`0.5 · noise · (1 − sweep)²`) keeps the resonator driven for the whole 300 ms sweep instead of only the initial burst.
- Neither change touches state, IDs, the 19 host parameters, the `.voxfx`/Program codecs or the frozen review set.

Measured after the change: formant spread **2.8 dB**, squelch spread **3.8 dB**, peaks remain bounded (formant ≤ .017, squelch unchanged in the low register) and DC stays below 1e-4.

The formant vowel still reads as a vowel, not as shaped noise: narrow third-octave probes at the prepared formant centres (492 Hz, 2980 Hz) against the gaps (800 Hz, 4300 Hz) give **+6.1…+19.6 dB** best-frame contrast at notes 36/60/84. That measurement is inside the new test, not a listening claim.

## New executable check

`tests/suite/FxFamilyDifferentiationTests.cpp` (CTest `suite_fx_family_differentiation_tests`, labelled `suite`) fails on the old code and passes now:

- all twelve family renders are finite, audible and bounded;
- **every** pair of families is spectrally distinct — closest pair whoosh/metallic distance `1.331`, threshold `0.90`, computed from octave-band shape, envelope decay, periodicity, spectral direction and roughness;
- no family swings more than 8 dB across MIDI 24…127 (**baseline fails at 16.6/22.8 dB**);
- formant peaks exceed the valleys by more than 2.5 dB at three octaves (**baseline fails at C6**).

## Regression evidence

- **41 focused engine tests pass** on this slice, including formant, squelch, laser, whoosh, impact, shepard, metallic, texture, glitch, granular, codec, transport, pattern and preset suites.
- All 232 review candidates still render deterministically at 44.1/48/96 kHz. Of 360 metric rows, 187 are byte-identical to the pre-change baseline and 162 change peak/level only within the rescaled families; no candidate crosses a quality limit: max peak `0.051026`, min mono/stereo RMS ratio `0.84005`, max DC `0.0002907`, max 5 ms edge `0.000195`. Raw rows: [family-depth-wave-a-232](evidence/family-depth-wave-a-232-20261119.txt).
- Determinism, uniqueness and the Wave B staged pairs still hold (`FxFactoryCandidatesTests` exit 0).

**Not qualified:** listening acceptance of the new formant/squelch timbre, full alias spectra, musical suitability in a mix, and Windows/native behaviour. The existing formant and squelch goldens in the review set were never audio-golden pinned, so no frozen render was rewritten.

## Verified at the committed source `e29b18a`

- **49/49 Linux executables** pass at `e29b18a9bcebc349d2193eb005e4233440316044`: [46 standalone](evidence/family-depth-inventory-20261119.txt) including the new CTest case, plus the [three pinned-JUCE native](evidence/family-depth-native-20261119.txt) processor, runtime-stress and export harnesses (0 processor failures, 0 stress failures, export create-only hashes unchanged).
- [ASan/UBSan](evidence/family-depth-sanitizers-20261119.txt) passed all seven relevant executables, including the new differentiation screen.
- Frozen audio is untouched: all nine legacy Granular float streams, all nine legacy Glitch WAVs and every fixed Delay pin still match their archived SHA-256 files on this commit.
- The 232 review candidates remain deterministic and inside the quality limits; 187 of 360 metric rows are unchanged and the rest move only where formant/squelch is in the program.
