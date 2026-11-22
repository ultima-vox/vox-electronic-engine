# Wavefold stationary mean and matched capture study

Follow-up to TL comment 7075841247 on #169. Parent FX engine: `c7ee608a0879087d9dade1f2471c69a1bde92732`; candidate engine source: `5aa2683d456256b9d69f7af937f80105fd89d381`. This is a Linux GCC `-O3` Chain-level diagnostic, not the full plugin, Windows FX-T18, a DAW render, or approval of the sound.

Reproduce with `python3 tools/compare_wavefold_stationary.py /tmp/vox-wavefold-study`. The script compiles the same capture harness against the parent header from Git and the current header. Two independent executions here produced byte-identical CSVs and WAVs. It does not alter the existing transient stress or its threshold.

## Stationary control and proposed test interpretation

For 44.1/48/96 kHz, one and four serial slots, wet 0/0.5/1 and amount 1, use a zero-mean, exactly opposite-half-cycle sine (period 256 samples, amplitude 0.7). Warm up for at least 0.5 seconds, then measure 65,536 samples (256 complete periods); check that output is finite, non-silent, and absolute mean is below 0.0001. All 18 parent and 18 candidate cases passed, with measured input and both output means zero at the precision of the fsum calculation. See `evidence/wavefold-stationary-dc-20261122.csv` for RMS and all cells. This is one balanced periodic stimulus, not a proof that arbitrary signals have no DC.

**Proposal for TL decision:** retain the existing 0.01 transient-window observation and its historical failure results without modification, and consider classifying it as a transient mean diagnostic rather than a standalone physical-DC pass/fail gate. Use a separately reviewed stationary DC test with settled, time-balanced signals for physical-DC qualification, extending its stimulus coverage before accepting a final contract. The transient failure remains open until this proposal is accepted; this experiment does not prove that the transient signal is DC-free. The independently failing Windows wall-deadline gate is unaffected.

## Phase and listening captures

`evidence/wavefold-stationary-phase-20261122.csv` gives fundamental amplitudes and phases at wet 0/0.5/1, one/four slots and all three rates. The one-slot wet-1 candidate differs from the parent by -1.40625 degrees for a 256-sample period, equivalent to one sample at that single frequency. Four nonlinear slots are not a linear delay: at 48 kHz, wet 1, their parent/candidate fundamental amplitudes are about 0.0144/0.1413 and their phase difference is 174.375 degrees. This illustrates a substantial signal-dependent tonal/phase change and cannot be extrapolated as a constant delay or comb response across frequencies.

### The response is a 3-point average, not a delay (added 2026-11-22)

The single 187.5 Hz measurement above is the low-frequency limit. A broader
low-level sweep ([evidence](evidence/wavefold-response-20261122.txt),
[CSV](evidence/wavefold-response-20261122.csv), 762 points, theory agreement
0.023 dB / 0.009°) through the real `Chain` shows ADAA2's linear response is
`S(ω) = (1+e^-jω+e^-j2ω)/3`, exactly the 3-point average. It has a **zero at
`rate/3`** and a 180° phase inversion above it. One wet-1 slot at amount 1 is
−1.6 dB at 5 kHz, −7.1 dB at 10 kHz and a null at 14.7 kHz (44.1 kHz); at
48 kHz the values are −1.3 dB, −5.8 dB and a null at 16 kHz. An exactly-fs/3
tone leaves ~3e-12 peak at amplitudes 0.001–0.9. Four serial slots compound
this to `S^4` (−28.6 dB at 10 kHz, 44.1 kHz). Wet 0.5 only partly fills the
null (worst grid point −18.6 dB).

This is the dominant sound change, not the one-sample group delay. It cannot be
described as a constant delay or a simple comb, and it must be resolved before
the alias improvement can be accepted. Moving ADAA2 behind 2×/4× oversampling
moves the null above the audio band (2fs/3 or 4fs/3) but needs PDC, prepared
filters and 6–20× the CPU; that decision is open.

The archive `evidence/wavefold-level-matched-20261122.tar.gz` contains parent/candidate 16-bit mono PCM pairs at each rate, SHA-256 `f1d3459d7ee70d350184d08119bd7c139592a926f78bafaa4655e3aa9943873e`. Each is a two-second synthetic signal through four slots: wet ramps from zero over 0.5 s, slot 1 switches to bypass from 1.0–1.15 s, then returns. Whole-file RMS is level-matched by attenuating the louder render, followed where necessary by the same peak-protection gain for both; exact gains and output hashes are in `evidence/wavefold-level-matched-20261122.csv`. These synthetic pairs support a future listening session but are not level-matched on every window and do not replace preserved host/program renders. No listening approval is claimed.

## Remaining blockers

The original and candidate still need the requested same-runner, same-settings Windows control with exact binary/source hashes, affinity, workload, all trial rows and warm/cold information. The existing Windows run has p99 deadline breaches and coarse 15.625 ms thread-time quantization, so CPU attribution is unknown; Windows locks were not probed. The approximately one-sample one-slot phase behavior and nonlinear four-slot differences need explicit sound/host approval, and the fs/3 passband null above must be fixed or explicitly accepted. #169 and #156 remain Draft/HOLD. No CI was launched for this diagnostic package.
