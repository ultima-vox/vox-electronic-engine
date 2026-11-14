# VOX Drums shared bar timing (PR #137 §10, revision 6)

Status:

- **Step 1 implemented** (`products/drums/engine/BarTiming.{h,inl,cpp}`): the pure timing module
  and the lossless-or-reject subdivision switch.
- **Step 2, not yet:** the scheduler, `DSEQ`, export and editor do not use it yet. See the
  plan below.

## Module

- **Time signatures:** n/4 (quarter beats), n/8 compound (n ∈ {6, 9, 12, 15}: dotted-quarter
  beats) and other n/8 (eighth beats). n is 1–16. 960 PPQ; beats of 960 / 1440 / 480 ticks.
- **Resolutions:** 8th, 16th, 32nd, 64th.
  - Straight and alternative (triplet, or duplet in compound meters) cells per beat.
  - `capacity()` gives full / straight-only / unavailable within 64 slots per bar, and
    `limitationReason()` the text shown to the user (§10.2 table).
- **`BarTiming`** (one lane's bar: signature, resolution, alternative-beat mask) gives:
  - beats, bar ticks, slots per beat, cells per beat, cell ticks;
  - integer cell start ticks;
  - the storage slot, and whether a slot is active (reserved slots are inactive).
- **`subhitTick()`:** ratchet subhits rounded **once** to the nearest tick, exact ties to the
  earlier tick. `subhitsDistinct()` checks a cell length / count pair.
- **`sampleOffset()`:** the one tick → sample conversion,
  `floor ((tick / 960 − blockStartPpq) × samplesPerQuarter)`.
- **`switchSubdivision()`:** moves a lane bar between layouts.
  - Each event keeps its absolute tick and all caller attributes (the template `Extra`).
  - It is rejected, changing nothing, on a collision, a |microTicks| > ½ cell, content in a
    reserved slot, or invalid timings.
  - A reverse switch may pick another cell on an exact tie (the TL example). Only Undo/Redo
    restores bit-identically.
- No allocation, no locks, no I/O.

## Tests (`suite_drums_bar_timing_tests`)

- **Capacity:** the §10.2 table exactly (4/4, 3/4, 6/8, 12/8, 7/8 × 4 resolutions), and the
  limitation reasons.
- **Sweep:** 300 (signature, resolution, mask) layouts. For each:
  - integer cell ticks;
  - strictly increasing starts inside the bar;
  - at most 64 slots;
  - active-slot count = cells.
  - Invalid masks are refused.
- **Ratchets:**
  - 40/3 → 0, 13, 27;
  - 30/4 ties → 7, 22;
  - distinct subhits for every supported cell length × 1–8.
- **Samples:** the conversion, including a negative offset.
- **Switches:**
  - 16th ↔ 16th-triplet;
  - the TL tie example (360 → triplet 320 + 40 → straight 240 + 120);
  - 12/8 straight ↔ duplet;
  - collision, half-cell overflow, reserved slot and other-signature rejections, each
    leaving the bar unchanged;
  - **20,000 randomized humanized trials:** 14,637 accepted, every absolute tick and attribute
    kept, offsets bounded, reserved slots empty; 4,220 rejected, each unchanged.
- **Mutation check (local):** making either tie rule pick the later tick fails the tests.

## Step 2 plan (the integration; one Draft PR, with the measured budget)

1. **`PatternStep`:**
   - `micro` (percent byte) is replaced by `microTicks` (`int16`), with the UI showing percent;
   - per pattern: signature (or "host"), resolution, and the alternative-beat masks
     (16 lanes × 4 bars).
   - `StoredPattern::resize` is **not** used for meter or resolution changes (it drops events).
     A lossless converter is used, or the change is rejected.
2. **`DSEQ` / `DSNG`, new versions:**
   - strict bounds; inactive slots must be empty;
   - `int16` microTicks: up to +4,096 bytes per full pattern, + masks;
   - the **full state measured** by the #130 T21 boundary fixture before acceptance.
3. **Scheduler (`KitEngine`):**
   - events come from `BarTiming` and `subhitTick`, converted with `sampleOffset`;
   - host bar lines, missing data and the jump threshold as in §10.4/§10.5a;
   - the uniform `stepLength` is removed.
4. **Export:** the same tick events as the scheduler; time-signature meta events.
5. **Generator and editor:** positions and cells through `BarTiming`.
6. **Tests:** the §10.5b matrix, including scheduler ↔ export equality at 44.1/48/96 kHz and
   64/333/512 blocks.
