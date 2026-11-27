# FX musical gain staging — audit, baseline and plan — 2026-11-27

**Development / Draft / HOLD / NO MERGE / NO RELEASE.** Part B of owner
acceptance FAIL
[#7223242731](https://github.com/ultima-vox/vox-electronic-engine/pull/171#issuecomment-7223242731)
(“очень тихо все FX”). Code baseline: `144662044be00cacb7ad543039ed01eac5adb605`.
This document records the measured baseline and the root-cause audit; the
calibrated implementation follows as a separate changeset with before/after
tables. No recipe, render or frozen inventory is modified by this document.
Drive/Wavefold transfer policy unchanged.

## 1. Signal-chain audit (at `1446620`)

Measured/located gain stages, in render order:

| # | Stage | Location | Value / behaviour |
|---|---|---|---|
| 1 | Source construction | `NoiseRiser.h` family branches | per-family amplitudes (e.g. `0.65·color+0.25·sin`, exciter `.30+.15·density`, `source*=0.15`) |
| 2 | Per-grain gain | `NoiseRiser.h` `grainGain` | `0.35 · velocity · fades · release · layerScale · layerGain · gainCurve` |
| 3 | Layer scale | `NoiseRiser.h` `noteOn` | `layerScale = 1 / max(1, enabledLayers)` (−12 dB per layer at 4 layers) |
| 4 | Voice sum | `render` loop | voices sum unbounded (no per-voice attenuation; polyphony headroom only) |
| 5 | **Master scale** | `NoiseRiser.h:396-397` | **`l * 0.055` = −25.2 dB, hard-coded, blind global** |
| 6 | FX chain | `Effects.h` `Chain::process` | serial wet mix `dry+(fx−dry)·wet`; default `wet=1` |
| 7 | Output param | `fx.output.gain` | range 0..1, **default 1.0**, 5 ms smoothing (`Processor.cpp:662`) |
| 8 | Preview bus | `engine/Preview.h` `mix` | crossfades captured PCM (levels as rendered; no extra gain) |
| 9 | Offline export | `offline::renderWav` | `settings.gain = output_->get()` (default 1.0) — same chain as realtime (tests prove sample-exact) |

Root cause of the owner’s “very quiet”: stage **5** alone removes 25.2 dB,
and stages 1–3 keep the internal mix small before it (median internal peak
≈ 0.14). There is no compensating gain anywhere downstream; output and
export default to unity, so the quietness is baked into the engine render.

## 2. Baseline measurement — all 232 frozen recipes

Harness: `tests/suite/FxGainStagingTests.cpp` (new). For every frozen
review recipe (Wave A `reviewEntries` 120 + Wave B 112 = 232) the capture
is rendered through the shipping offline path (identical to realtime)
at **normalized note 60, velocity 1.0, 120 BPM, output = 1.0**, then
analysed for full-file peak, active-window RMS (configured event window),
crest, tail RMS, silence and clipping.

Raw per-recipe table:
[evidence/gain-baseline-1446620.tsv](evidence/gain-baseline-1446620.tsv)
(232 rows; SHA of run recorded below).

### Aggregate baseline (dBFS)

| Group | n | peak p10 / p50 / p90 | active RMS p10 / p50 / p90 | silence | clip |
|---|---|---|---|---|---|
| **ALL** | 232 | **−49.6 / −42.5 / −35.1** | −67.4 / −58.9 / −50.3 | 0 | 0 |
| impact | 29 | −45.6 / −38.1 / −33.8 | −63.4 / −53.2 / −46.4 | 0 | 0 |
| riser | 32 | −48.5 / −41.7 / −36.4 | −67.2 / −59.7 / −56.5 | 0 | 0 |
| fall | 29 | −48.4 / −43.9 / −38.0 | −64.2 / −60.1 / −52.2 | 0 | 0 |
| sweep | 28 | −51.8 / −43.9 / −36.8 | −70.8 / −60.9 / −54.0 | 0 | 0 |
| zap | 29 | −48.6 / −39.5 / −34.2 | −63.5 / −56.6 / −47.6 | 0 | 0 |
| rhythm | 29 | −47.8 / −41.2 / −33.6 | (sparse) / −57.7 / −49.0 | 0 | 0 |
| atmosphere | 27 | −54.4 / −46.5 / −42.8 | −69.5 / −59.6 / −55.8 | 0 | 0 |
| stinger | 29 | −50.9 / −43.3 / −38.8 | −65.8 / −59.8 / −52.8 | 0 | 0 |

Quietest: `voxfx.riser.meridian-gauze` −55.5 dB peak. Loudest:
`voxfx.rhythm.neon-fault` −27.9 dB peak. **No recipe is even close to
0 dBFS — the loudest of 232 sits 28 dB below full scale; the median sits
42 dB below.** Path checks: engine-direct (MIDI) render equals the WAV
render (sample-exact for one-shot fixtures; existing offline tests prove
full parity), and output = 0.5 scales everything linearly (−6.02 dB).

## 3. Proposed category targets (for owner approval)

Anchor: “usable alongside Drums/Bass at unity DAW fader” (owner). Drums
factory pads play source buffers measured at peak 0.5–0.8
(`DrumsSampleTests`) through pad level ≤ 1.0 — i.e. one-shots land roughly
−6…−3 dBFS. Proposed FX targets (single event, velocity 1.0, output 1.0):

| Category (role) | peak target | active RMS target |
|---|---|---|
| impact, zap, stinger, rhythm (one-shot accents) | −6…−3 dBFS | ≤ −18 dBFS |
| riser, fall, sweep (movement) | −3…0 dBFS at the peak moment | −16…−12 dBFS active |
| atmosphere (beds) | −9…−6 dBFS | −20…−16 dBFS |

Headroom rules (measured, not assumed): no clipping for a single event;
worst-case **8-voice + 4-layer + 4-Drive-cascade** render stays ≤ −1 dBFS
or the excess is explicitly documented; overshoot/tails included. No
limiter, no threshold weakening, no blanket peak normalization; dynamics
between quiet beds and loud hits must survive (target spread ≈ 15–20 dB
between atmosphere p50 and impact p90, versus the accidental 27 dB recipe
spread seen today).

## 4. Implementation plan (separate changeset)

1. **Per-family calibrated trims** at the layer stage (stage 1–2): measure
   each of the 12 families in isolation (one layer, gain 1, velocity 1)
   and apply named, documented trims so families land on a common family
   target — this compresses the accidental inter-recipe spread instead of
   one blind multiplier.
2. **Replace the blind `0.055` master scale** (stage 5) with a named,
   measured constant chosen so the p90 single-event peak meets §3 with
   polyphony headroom (measured at 1/4/8 voices).
3. Keep `layerScale` (layer-count normalization), velocity response,
   envelopes, FX wet behaviour, `fx.output.gain` range/default, preview
   and export paths untouched semantically — output/export simply become
   audible because the engine render is calibrated.
4. Justification for the TL’s Drive/Wavefold clause: family/master staging
   changes **level into** the Drive transfer, not the transfer itself
   (memoryless curve, ADAA, amount semantics unchanged). Before/after Drive
   RMS/DC compatibility table will be re-run and attached; if the owner
   prefers zero drive-input change, the alternative is a post-chain trim
   only — recorded as an explicit decision point.
5. Regressions added/extended: 232-recipe non-silence + no-clip (already in
   `FxGainStagingTests`), category band assertions after calibration, 8-voice
   worst-case headroom assertion, output=0.5 ≡ −6.02 dB linearity,
   preview/offline/realtime level parity.

## 5. Status

- [x] Chain audit, baseline table for all 232, path comparison — this doc.
- [x] Non-silence/no-clip suite test `suite_fx_gain_staging_tests` (0 failures
      at baseline; full suite label re-run pending environment restore).
- [ ] Family-isolation measurements and calibrated trims.
- [ ] Master-scale recalibration + polyphony headroom measurements.
- [ ] Before/after table, Drive compatibility re-run, owner review.

---

## P1 harness corrections (TL review 7224771869, applied)

The baseline commit `a65fbd7` was accepted as diagnostic-only; its measurement
harness required corrections before it can support a calibrated gain change.
All P1 items from the review are now implemented in
`tests/suite/FxGainStagingTests.cpp` (suite target
`suite_fx_gain_staging_tests`):

| Review item | Correction |
| --- | --- |
| assetCue captures bypassed the user sample | Recipes now go through `content::waveb::makeAssetPreview` and decode the authored WAV with `sample::decodeWav` (supported path), publishing `capture.sample`/`capture.sampleRate` to the store. Missing decodes are collected separately (`missingAssets`, must be empty — currently 0). |
| Row count instead of ID-set equality | The measured recipe ID set is compared for exact equality against the canonical inventory ID set (232 IDs), plus an explicit `inventory == 232` gate. |
| Levels measured from PCM24 bytes | Levels now come from `renderFloats`, a line-faithful replica of `renderWavImpl` (same setup order, scheduling, `macro::renderPost`), measured pre-encoding. Status is distinguished: `invalid` / `setup-failed` / `overload` / `ok`; silence is an analysis property of an ok render. |
| No status distinction | `renderFloats` reports `invalidCapture` (bad program/settings), `setupFailed` (any engine/chain/macro step returns false), `overload` (non-finite or abs>1 sample — the exact `renderWavImpl` reject condition), or `ok`. All 464 baseline renders reach `ok`. |
| printf-only path checks | Controlled legs (6 fixtures: oneshot / bars / gate / sequence / both asset cues), each with asserted tolerances: (A) `renderWav` float32 is **bit-identical** to the replica floats; (B) two **independent captures** gain 1.0 vs 0.5 are sample-wise linear (max abs err <= 1e-6); (C) PCM24 header is stereo 24-bit at 48 kHz and quantization <= 1 LSB (measured 0.5 LSB) against float32; (D) Preview Bus mix reproduces published PCM exactly (max err 0) after the 5 ms crossfade. 30/30 leg checks pass. |
| Algebraic `outHalfDiff` | Removed entirely; leg B replaces it with real independent renders. |
| Engine-direct path lacked the full pipeline | Removed; the replica IS the full pipeline (tempo, bar clock, voice motion, macro bases, user-sample store, program, preview/pattern noteOn, chain, effects, `renderPost` with the output gain) and is asserted byte-equal to the shipping float32 export path. |
| Sequence active window | Sequence recipes use exactly one pattern cycle as the active window (`cycleFrames` from `tickToSample`); bars/oneshot use the event window. Active and tail peaks are reported separately (new `tail_peak_dbfs` column). |
| Authored baseline | Every recipe now has two TSV rows: `authored` (frozen note/BPM/velocity/duration from the recipe row) and `normalized` (note 60 / velocity 1.0 / 120 BPM). Authored peak p50 = **-42.6 dBFS**; normalized peak p50 = **-42.5 dBFS**. |
| Runtime bound | Full run (464 renders + 30 leg checks) = **47.5 s**; full suite ctest = 79/79 in 256.9 s. |

PCM24 quantization vs float32 is documented in the harness header: PCM24 is
only used for format-correctness and the 1 LSB check; no sample-exact claim is
made from quantized bytes (float32 export is the bit-exact reference).

Evidence: `evidence/gain-baseline-p1-20261127.tsv` (464 recipe rows + 12 leg
rows; header documents all columns).

Baseline numbers are unchanged by the corrections (normalized variant):
peak p10/50/90 = -50.0 / -42.5 / -35.1 dBFS, active RMS p50 = -58.9 dBFS,
quietest `voxfx.riser.meridian-gauze` -55.5 dB, loudest
`voxfx.rhythm.neon-fault` -27.9 dB, 0 silent, 0 overloaded, 0 above -5 dBFS
headroom floor. The two asset cues now measure their authored WAVs
(`voxfx.sweep.vellum-coil` -55.7 dB, `voxfx.atmosphere.kiln-choir`) instead
of the builtin fallback.

**No DSP change is included in this commit** — the harness correction is
measurement-only. The P0 gain design (post-chain makeup vs pre-chain trim,
polyphony/velocity/tail coverage) remains open per the review; the UI rebuild
runs in parallel and is not gated on it.
