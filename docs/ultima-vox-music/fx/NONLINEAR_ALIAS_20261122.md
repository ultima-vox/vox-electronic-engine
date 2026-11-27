# FX-04 nonlinear quality tranche — Drive and Wavefold

> **Superseded 2026-11-27.** The shipping Wavefold is no longer ADAA: it was
> restored to the original memoryless transfer and ADAA moved under
> `experimental::`. See [WAVEFOLD_RESTORE_20261127.md](WAVEFOLD_RESTORE_20261127.md).
> This document remains the record of the Drive ADAA decision and the
> superseded ADAA-Wavefold measurement method.

**Development candidate only / Draft / HOLD / NO MERGE / NO RELEASE.** The
current candidate source is `a6365ebf88414c4ff5813df396e5b12f2fc8e49b` on the
assigned Arena branch. This slice is not a Windows, VST3-validator, DAW,
timing, listening, or release qualification result. Original host parameter
IDs and the original Wavefold type ID are unchanged.

## Source changes

`products/fx/engine/Effects.h` now keeps one previous input sample per effect
slot and applies first-order antiderivative antialiasing (ADAA) to both
nonlinear slots:

- Drive retains the existing gain-compensated parallel shape and its stable
  `log(cosh)` antiderivative.
- Wavefold retains the existing triangle fold transfer function. Its exact
  periodic antiderivative is evaluated as a divided difference across the
  previous/current input samples. The period integral is zero, so the phase is
  reduced to `[0, 4)` without a lookup table, allocation, oversampling, or
  latency.
- Amount zero remains transparent. Type changes clear both histories in the
  existing bounded per-slot state. No Program codec or host-ID field was
  changed by this quality slice.

The editor now names the existing Amount lane according to the selected type
(`Saturation`, `Fold depth`, `Time / feedback`, `Feedback`, and so on) and
provides a type-specific tooltip. This is a semantic/UI contract over the
existing host lanes, not a new parameter-ID family.

## Local executable evidence

`tests/suite/FxNonlinearAliasTests.cpp` compares a bin-centred point-sampled
reference with the rendered slot at 44.1, 48 and 96 kHz, three high-frequency
tones, and amounts 0.5 and 1.0. It checks finite/in-range energy, Drive's
existing reduction gate, Wavefold non-increase against its direct reference,
and zero-amount transparency.

Observed local output from the current working tree:

- 44.1/48/96 kHz, amount 1.0, middle probe: direct Wavefold `0.8684`, ADAA
  Wavefold `0.1259` (about `-0.6` to `-9.0 dB` folded-energy ratio).
- 44.1/48/96 kHz, highest probe, amount 0.5: direct `1.0000`, ADAA `0.2272`.
- Executable result: `FX nonlinear alias: 0 failure(s)`.

`tests/suite/FxEffectsTests.cpp` also checks the type-specific amount
contract, all effect types, finite bounded output, block invariance, four-slot
behavior, delay/tail behavior, and zero-amount semantics. Local result:
`FX effects foundation: 0 failure(s)`.

These are standalone Linux C++ fixtures. No Windows render, loaded runtime,
host bounce, validator result, or perceptual approval is claimed. FX-T18
remains OPEN, and the exact integrated source lineage still requires owner
reconciliation before any Windows run.

## Listening and remaining gates

The frozen listening inventory remains exactly `232 review-only / 0 approved`,
with the 600 approved-program target unchanged. The slightly smoother/darker
nonlinear result needs an owner listening pass in the existing handoff before
any content or quality sign-off. Do not transfer raw audio without an
owner-authorized destination and recipient list.

Remaining P0/P1 gates are exact integrated-source re-pin, Windows x64 Release
build/validator/install evidence, loaded callback/tail evidence, and one
owner-authorized Cubase/REAPER round. This document does not create a
production receipt or alter the frozen FX-T18 parser package (`1.11.0`).
