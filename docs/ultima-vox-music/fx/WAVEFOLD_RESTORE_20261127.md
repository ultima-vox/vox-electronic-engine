# FX-04 Wavefold shipping restore and nonlinear regression hardening — 2026-11-27

**Development candidate / Draft / HOLD / NO MERGE / NO RELEASE.** Code
candidate for this checkpoint: `1625719fd5755fa1516d058b0b272a963aec33c9`
on the assigned Arena branch (parent `2e6b0c70a29d1f0486fcc23d0410c48d0882e94f`).
This document answers TL review comment
[#7211163434](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7211163434)
on PR #156. It is not Windows, VST3-validator, DAW, loaded-RT, listening, RC,
merge or release evidence. Host parameter IDs, the Wavefold type ID, the
frozen diagnostic parser `1.11.0`, the create-only WAV/MIDI export policy and
the frozen `232 review-only / 0 approved` inventory are unchanged.

## 1. Shipping Wavefold restored (P1)

`products/fx/engine/Effects.h`:

- The `Chain` Wavefold slot is again the **original memoryless triangle fold
  with the ±8 input clamp** — `wavefoldTransfer(value, gain)`, mathematically
  identical to the transfer that shipped at PR #156 `5c2f122`
  (`Effects.h` original `fold` lambda). There is no history, no zero-amount
  bypass branch and no per-slot fold state; `State::foldPrevious*` was removed
  with the reset in `Chain::set`.
- A standalone differential harness compared `wavefoldTransfer` against the
  verbatim `5c2f122` expression over **4,000,000 samples** (uniform inputs
  ±4 across all amounts, plus over-range ±16 at amount 1.0 where the clamp
  binds): **0 bit mismatches**. Sound, phase/transient response and legacy
  project bounces therefore match the pre-change implementation for the same
  input, as required by the review.
- `amountHelp(Type::wavefold)` no longer claims antialiasing; it now reads
  “Original triangle fold depth (memoryless); amount 0 is transparent”. The
  Drive help still correctly describes ADAA, which Drive keeps.

## 2. ADAA Wavefold kept experimental (P1)

- First-order ADAA remains compiled and executable as
  `vox::fx::effects::experimental::wavefoldAdaa(value, previous, gain)` with
  `experimental::wavefoldIntegral`, clearly marked **not the shipping path**,
  pending explicit owner approval and listening.
- `Chain` never calls it. Enabling it later is a deliberate, reviewable
  wiring change, not an accident of this checkpoint.
- The nonlinear fixture measures the experimental curve on every probe
  (folded energy must never exceed the direct transfer and must strictly
  decrease wherever folding exists), so the experimental candidate stays
  executable without changing shipping sound.

## 3. Strengthened nonlinear DSP regression (P1)

`tests/suite/FxNonlinearAliasTests.cpp` was rewritten against the review
list:

- **Explicit success.** `analyse` returns `ok=false` on non-finite or silent
  output; `measure` returns `ok=false` when `prepare()`/`set()` fails. Every
  probe asserts prepare/set success **and** a measurable RMS/peak floor
  before any ratio is used. A zero alias ratio on silence or a failed setup
  can no longer pass.
- **Zero-amount transparency is a real assertion.** For both Drive and
  Wavefold, amount 0 is compared **sample by sample** against the input
  (max error < 1e-6) with measurable output level; the alias ratio is only a
  secondary generated-energy guard, not the transparency claim.
- **Actual pre-change reference.** The Wavefold reference is the original
  memoryless transfer **including the ±8 clamp**, evaluated on the same
  float samples the Chain consumes. The shipping render must match it
  (max diff < 1.5e-4, bounded by the wet/amount smoother’s half-ulp
  convergence) at amplitudes 1.0 **and 2.0**, i.e. with the clamp actually
  bound — the fixture asserts the unclamped reference differs by > 1e-3, so
  the clamp path is exercised, not theoretical.
- **Measured phase/latency.** A lag scan (±4 samples) must select lag 0 and
  a one-sample shift must be > 100× worse. “No buffering/PDC” is never used
  as a latency argument.
- **Passband.** Below the fold knee the transfer is the exact linear gain
  `(1+7·amount)`; level must match sample-by-sample, > 99 % of energy must
  stay on the input fundamental, and lag must be 0.
- **Transients.** The shipping impulse response maps one sample through the
  transfer then produces *exactly* zero (memoryless, no tail/ringing); the
  step response is a constant equal to the closed-form fold value. Drive’s
  ADAA impulse settles to exact silence within two samples.
- **Dry/wet.** wet 0 is bit-exact dry, wet 1 is the full transfer, wet 0.5
  is the arithmetic midpoint and differs from both endpoints.
- **Automation.** The per-slot modulation array (host automation lane
  equivalent) is exercised: amount +0.5 converges to the amount-1 transfer,
  wet −1 clamps to bit-exact dry, out-of-range amount clamps at the host
  maximum, output stays finite and bounded.
- **Boundaries.** `prepare` rejects 0/7999/384001/NaN/∞, `set` rejects
  slot 4, NaN/1.5/−0.1/2 amounts, NaN wet, unknown type; amount and wet 1
  are accepted and reference-matched with a measurable non-silent floor.
- **Drive old-project compatibility (automated part).** Shipping ADAA Drive
  is compared with the legacy point-sampled transfer on identical input:
  RMS within 5 % (observed ≤ 3.5 %: 0.3093→0.3013, 0.3254→0.3147,
  0.3143→0.3039 at amounts 0.3/0.7/1.0), DC within 0.01 (observed 0.00000),
  peak within 5 %. This is compatibility evidence only; **listening
  evidence remains an open owner gate** and alias metrics alone are not
  acceptance for Drive.
- The stale fixture comments (point-sampled Wavefold as “remaining gap”,
  ADAA wording) were replaced with the current shipping contracts.

### Executed results at `1625719`

| Test | Command | Result |
|---|---|---|
| FX nonlinear alias | `g++ -std=c++20 -O2 -Wall -Wextra -pedantic -I products/fx tests/suite/FxNonlinearAliasTests.cpp && ./a.out` | `FX nonlinear alias: 0 failure(s)` (44.1/48/96 kHz, 18 alias probes + 8 sections) |
| Same, ASan+UBSan | `g++ -std=c++20 -O1 -g -fsanitize=address,undefined …` | `0 failure(s)`, no sanitizer reports |
| Wavefold differential | 4 M-sample harness vs `5c2f122` expression | `0/4000000 bit mismatches` |
| FX effects foundation | `tests/suite/FxEffectsTests.cpp` | `FX effects foundation: 0 failure(s)` |
| FX sound graph | `tests/suite/FxSoundGraphTests.cpp` | `FX sound graph/deterministic routes: 0 failure(s)` |
| FX effect tails | `tests/suite/FxEffectTailTests.cpp` | `0 failure(s)` |
| FX macro routing | `tests/suite/FxMacroRoutingTests.cpp` | `0 failure(s)` |

Local Linux tooling only (`g++ 12.2`, no JUCE, no CTest label `suite` run in
this table). No Windows, no host, no listening.

## 4. Remaining gates for this slice

1. Owner listening decision on ADAA Wavefold (keep experimental or approve)
   and on Drive’s ADAA character versus legacy projects.
2. Exact-HEAD Windows x64 Release suite + Steinberg validator (separate
   authorization required; not dispatched here).
3. Attribution note: any audio rendered before this restore was produced by
   the ADAA fold; frozen review-inventory renders are **not** re-attributed
   to the current engine until an owner-authorized exact-recipe
   rerender/compare. The frozen inventory itself is untouched.
