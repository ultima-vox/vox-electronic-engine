# FX-04 Wavefold — first-order ADAA experiment

**Draft / HOLD / NO MERGE.** Experimental branch
`arena/a7cf1349-vox-electronic-engine`, on top of `f007994`. ADAA2 is rejected:
its three-point average nulls `fs/3` ([response evidence](evidence/wavefold-response-20261122.txt)).
This slice tests **first-order ADAA (ADAA1)** as the next experimental option,
per the TL decision. The original point-sampled Wavefold remains the production
base; nothing here changes `next/fx-development`.

Static transfer function, FXSL type id 3, 6-bit packing, v1 legacy decode and
the entire Drive ADAA path are unchanged. No oversampling, compensation filter,
PDC or new parameter is introduced.

## Change

`products/fx/engine/Effects.h` keeps the exact triangle fold and evaluates it
with the exact first antiderivative `F`:

```text
y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
     = f((x[n] + x[n-1]) / 2)        when the inputs coincide
```

On a linear segment this is the two-point average `(1+z^-1)/2`: **half a
sample** of group delay, with a roll-off toward Nyquist but **no in-band
null**. Per channel the slot stores one previous input plus its cached `F`
value (three doubles); the cache is rebuilt when the smoothed amount moves the
gain. Non-finite input clears the history. The outgoing slot's history is kept
through a type-change crossfade, and the incoming slot is value-initialized
when the transition starts, as fixed for ADAA2.

## Aliasing, one slot (44.1 kHz; 48/96 kHz are identical in relative terms)

Absolute folded power relative to a unit sine, and folded/output ratio in dB.
`Δ abs` is the improvement; `Δ ratio` is the improvement relative to the slot's
own output, which is the stricter view.

| Tone | Amount | Point abs | ADAA1 abs | Δ abs | Point ratio | ADAA1 ratio | Δ ratio |
|---|---|---|---|---|---|---|---|
| 1.84 kHz | 0.28 | −24.9 | −31.2 | +6.3 | 24.0 | 30.0 | +6.0 |
| 1.84 kHz | 0.50 | −22.3 | −32.8 | +10.5 | 19.5 | 29.2 | +9.7 |
| 1.84 kHz | 1.00 | −11.4 | −17.1 | +5.7 | 9.1 | 12.8 | +3.7 |
| 4.59 kHz | 0.28 | −9.9 | −15.4 | +5.5 | 9.0 | 12.4 | +3.4 |
| 4.59 kHz | 0.50 | −3.4 | −9.0 | +5.6 | 0.6 | 1.0 | +0.4 |
| 4.59 kHz | 1.00 | −2.9 | −16.5 | +13.6 | 0.6 | 9.0 | +8.4 |
| 7.35 kHz | 0.28 | −2.2 | −7.7 | +5.5 | 1.3 | 0.9 | **−0.4** |
| 7.35 kHz | 0.50 | −2.7 | −17.3 | +14.6 | 0.0 | 6.4 | +6.4 |
| 7.35 kHz | 1.00 | −2.7 | −9.9 | +7.2 | 0.4 | 2.9 | +2.5 |

The absolute improvement is 5.5–14.6 dB, but part of it is attenuation of the
whole output: relative to its own level, ADAA1 gains 4.4 dB 0.4 dB or less at
4.59 kHz/0.5 and 7.35 kHz/0.28, and is 0.4 dB *worse* at 7.35 kHz/0.28. This is
not the clear alias win that ADAA2 showed on the same grid.

Raw run: [wavefold-adaa1-alias-20261122.txt](evidence/wavefold-adaa1-alias-20261122.txt).

## Frequency response

Low-level bin-centred sweep through the real `Chain` (762 points; measured vs
the exact two-point-average theory to 0.0015 dB / 0.006°), candidate minus
point-sampled parent. One wet-1 slot, amount 1:

| rate | 1 kHz | 5 kHz | 10 kHz | 15 kHz | ≈19.5 kHz |
|---|---|---|---|---|---|
| 44.1 kHz | −0.02 | −0.57 | −2.37 | −6.19 | −14.74 |
| 48 kHz | −0.02 | −0.48 | −1.97 | −4.98 | −10.67 |
| 96 kHz | −0.00 | −0.12 | −0.46 | −1.07 | −1.90 |

There is no mid-band null: at fs/3 the relative loss is 6.02 dB, the expected
two-point average, not a notch. The roll-off is still audible near Nyquist, and
**four serial slots compound it**: at amount 0.28 they lose 9.5 dB at 10 kHz,
24.7 dB at 15 kHz and 58.9 dB at 19.5 kHz (44.1 kHz). Raw:
[wavefold-adaa1-response-20261122.csv](evidence/wavefold-adaa1-response-20261122.csv),
[summary](evidence/wavefold-adaa1-response-20261122.txt); reproduce with
`python3 tools/compare_wavefold_response.py OUT` (the default `--method adaa1`;
`--method adaa2` reproduces the rejected prototype).

Measured group delay is **0.500 sample** at all three rates, versus 0.198 for
Drive at amount 0.28. No integer latency, buffer or PDC change.

## Automation, type change and four slots

The stationary study ([DC](evidence/wavefold-adaa1-stationary-dc-20261122.csv),
[phase](evidence/wavefold-adaa1-stationary-phase-20261122.csv)) uses a balanced,
exactly periodic sine, 0.5 s settle, 65,536 samples, 1/4 slots, wet 0/0.5/1:

- all 18 candidate cells are finite, non-silent and have zero measured mean,
  like the parent; the original 0.01 transient-window criterion is untouched;
- one-slot wet-1 phase at 172/187/375 Hz is −0.70°, i.e. **0.5 sample**;
- four slots at wet 1 are not a linear delay at normal levels: the fundamental
  amplitude and phase are signal-dependent (44.1 kHz: parent 0.0142 vs
  candidate 0.0580, phase difference −2.81°).

Level-matched parent/candidate pairs (wet ramp 0–1 over 0.5 s, then a bypass
switch on slot 1) are archived with hashes:
[wavefold-adaa1-level-matched-20261122.tar.gz](evidence/wavefold-adaa1-level-matched-20261122.tar.gz)
and [CSV](evidence/wavefold-adaa1-level-matched-20261122.csv). They are
synthetic Chain captures, not host renders or listening approval.

Four serial slots separately: alias improves by only **1.9–4.0 dB absolute**
and **0.001–1.2 dB relative** at 4.59/7.35 kHz, amounts 0.5/1, while the
passband loss reaches 24–59 dB near 19.5 kHz. The four-slot trade-off is poor.

## CPU

Focused four-serial-slot section, 96 kHz, 128-sample blocks, p50 of 20,000
blocks, three interleaved runs, median:

| Workload | Point (parent) | ADAA1 | ADAA2 (rejected) |
|---|---|---|---|
| Wavefold amount 0.5 | 49.78 µs | **25.30 µs** | 30.62 µs |
| Wavefold amount 1.0 | 48.72 µs | **26.53 µs** | 32.25 µs |
| Drive amount 0.5 | 38.6 µs | 38.6 µs | 38.6 µs |

ADAA1 is about 46–49 % cheaper than the old `asin`/`sin` path (which evaluated
`acos(-1.0)` twice per sample) and about 17 % cheaper than ADAA2. The container
was noisy (whole-engine rows varied 2×); these are single-machine numbers, not
a Windows or deadline claim. Raw: [wavefold-adaa1-cpu-20261122.txt](evidence/wavefold-adaa1-cpu-20261122.txt).

## Verification

Linux, GCC 12.2.0, direct builds (no CMake/CTest or JUCE toolchain here):

- 8/8 standalone suites pass: effects, nonlinear alias (including the new
  four-slot and fs/3 response checks), offline WAV, effect tails, bar events,
  sound graph, portable preset, factory candidates;
- ASan/UBSan pass for the alias, effects and tail suites;
- near-coincident mean-value error 2.2e-8; parallel dry/wet contract 2.7e-6;
  rapid type/wet changes remain bounded (max step 0.0031);
- the alias limits are method-specific: ADAA1's measured worst cases are 28 %
  of the old folded power and 17 % of input power, versus the ADAA2 limits of
  20 %/10 %. The fs/3 response check would fail if a three-point average (or a
  null) were reintroduced.

The ADAA2 Decimal fixture and generator remain in the tree as archived
evidence, but the test no longer includes them. All ADAA2 and failed Windows
stress evidence is preserved unchanged, including the 44/72 transient-window
failures and the 99/86/89 wall misses; the stationary study does not hide them.

## Assessment

ADAA1 is cheaper and has no in-band null, but its alias benefit is modest and
mostly comes with a top-octave loss that compounds in serial slots. At four
slots and high amounts the relative alias improvement is at most about 1 dB
while the response loses tens of dB near 19.5 kHz. On this evidence **I would
keep the original Wavefold as the production base** and not replace it with
ADAA1; a real alias improvement needs the oversampling/PDC architecture
decision that the TL has deliberately deferred. This is a recommendation, not
an acceptance: listening and host qualification remain open, and the final
choice is the TL's.

#169 stays Draft/HOLD/NO MERGE. No CI was launched for this experiment. FX-T18,
Windows, OLE/Cubase/REAPER and listening remain open.
