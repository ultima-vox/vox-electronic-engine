# VOX Drums shared bar timing (PR #137 §10, revision 6)

Status:

- **Step 1 implemented** (`products/drums/engine/BarTiming.{h,inl,cpp}`): the pure timing module
  and the lossless-or-reject subdivision switch.
- **Step 2 implemented** (PR #140): one cell model for storage, scheduler, export, song,
  generator and the old editor, through `BarTiming`. See [Step 2](#step-2-implemented) below.
- **TL decisions (#140 comment 6882837766), all applied:**
  - swing option A;
  - the legacy `ceil` sample rule;
  - a 4-byte packed state cell.

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
- **`sampleOffset()`:** the one time → sample conversion. An event sounds on the first sample at
  or after it: `ceil ((eventPpq − blockStartPpq) × samplesPerQuarter − 1e-6)`, the legacy rule
  that keeps DRM-00.
  - It is used for integer-tick events and for unrounded swung times.
  - The result is negative before the block, and ≥ the block length after it.
- **`swingDelay()`:** the one swing rule.
  - A straight cell whose index in the bar's straight grid (beat × straight cells + cell) is odd
    is late by `swing × (cellTicks / 960.0)` quarters. The phase restarts at every bar line.
  - Tuplet cells are never swung.
  - In the uniform 4/4 grid this is the legacy `swing × step`, with the same floating-point
    operation order.
- **`exportTick()`:** a continuous time rounded once to the nearest tick, an exact tie going to the
  earlier tick.
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
- **Samples:**
  - exact samples, the 1e-6 tolerance, fractional positions, negative offsets;
  - every event lands in exactly one block at 44.1/48/96 kHz × 64/333/512.
- **Swing:**
  - 4/4 at 16th/32nd/64th, 3/4, 6/8 (straight and duplet beats), 7/8 at 8ths;
  - tuplets are never swung;
  - `exportTick` ties.
- **Switches:**
  - 16th ↔ 16th-triplet;
  - the TL tie example (360 → triplet 320 + 40 → straight 240 + 120);
  - 12/8 straight ↔ duplet;
  - collision, half-cell overflow, reserved slot and other-signature rejections, each
    leaving the bar unchanged;
  - **20,000 randomized humanized trials:** 14,637 accepted, every absolute tick and attribute
    kept, offsets bounded, reserved slots empty; 4,220 rejected, each unchanged.
- **Mutation check (local):** making either tie rule pick the later tick fails the tests.

## Step 2 plan (as written before step 2; see "Step 2 (implemented)")

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

## Step 2 (implemented)

### Cell model

- **`kit::PatternStep`:** `microTicks` (`int16`, ticks of the 960-PPQ timeline, |value| ≤ ½ cell)
  replaces the percent byte.
  - `kit::percentToTicks` is the one quantisation of a percent edit (nearest, ties earlier).
  - 8 bytes in memory.
- **`StoredPattern` / `BarCells`** (a unique song bar):
  - meter, resolution, `followHost` (pattern only);
  - per lane and bar a 16-bit alternative-beat mask;
  - cells in fixed storage slots (`BarTiming::slot`).
  - Unused slots (past the capacity, or reserved by a beat's subdivision) are always empty.
  - `valid()` checks every rule of §10.5.
- **Grid changes:**
  - `convertGrid` (meter and/or resolution) and `switchBeats` (subdivision) are lossless or
    rejected atomically, with the place named. They are built on the generalised `convertBar` and
    on `switchSubdivision`.
  - `StoredPattern::resize` is **removed**. `setBars` only changes the bar count (bars dropped,
    or new bars repeating bar 1).

### Formats (development versions are not decoded, #136 policy)

- **`DSEQ` v4:**
  - per pattern: grid (u8 numerator, u8 denominator, u8 resolution), u8 followHost, u8 bars,
    u32 seed;
  - 16 × bars u16 masks;
  - 16 × bars × slotsPerBar cells, **each one 32-bit little-endian word** (TL option B;
    `microTicks` stays `int16` in memory).
  - Strict, decoded before commit: grid, mask, every value, |micro| ≤ ½ of the actual cell,
    distinct ratchet subhits, zero reserved bits, empty reserved slots, no truncated or trailing
    bytes. Values of `on = false` cells in active slots are kept.

  | Bits | Field |
  |---|---|
  | 0, 1, 2 | on, accent, locked |
  | 3–5 | ratchet − 1 (1..8) |
  | 6–12 | velocity (0..127) |
  | 13–19 | probability (valid: 1..100) |
  | 20–28 | microTicks, 9-bit two's complement (valid: ±½ cell, ≤ 240) |
  | 29–31 | reserved, must be 0 |
- **`DSNG` v2:** unique bars carry grid, seed, 16 masks and their cells in the same layout. There
  is no song meter.

### Playback and export

- **One rule, `kit::cellHits`:**
  - tick = bar line + cell start + microTicks;
  - ratchet subhits through `subhitTick`;
  - probability key: (seed, absolute bar × 64 + slot, lane).
  - Scheduler and both exports use it.
  - The uniform `stepLength`, `stepHits` and `songHits` are removed.
- **Swing (TL option A):** `timing::swingDelay`.
  - Playback adds it unrounded, which keeps DRM-00 bit-identical.
  - The MIDI export writes `exportTick` of the same continuous time.
  - **The one exception to tick-identical export:** a swung event can differ from what plays by
    at most ½ tick. Every unswung event, ratchet subhits included, is tick-identical.
- **Time → sample:** `timing::sampleOffset` (the legacy `ceil(x − 1e-6)`), the scheduler's only
  rule.
- **Bar lines (§10.4), `kit::barGrid`:**
  - With a host bar position: the host's bar start and index, and its meter (when supported).
  - Otherwise: bars of the pattern's meter (a song: 4/4) counted from 0.
  - A pattern bar plays from its bar line. When longer than the host's bar it is cut at the next
    line; when shorter it rests until that line. Patterns and the export are never changed.
  - A song bar follows the host's bar index.
  - A step back of more than one tick restarts the scheduler; less is jitter.
  - ADVANCED shows "Host gives no bar positions; bars counted from the start".
- **Export:**
  - a pattern exports in its own meter, with its time-signature event;
  - a song exports bar by bar in each bar's meter (an empty bar is 4/4), with a time-signature
    event at every change.
  - NoteOff stays at half a subhit (as before).

### Generator

- It writes 4/4. Generating in another meter is refused, changing nothing, until the styles'
  meter rules are interpreted (§10.6).
- A cell starting on a 16th gets that position's hit. Micro is a percent of that cell, quantised
  once. The lane's other cells are cleared.
- **Equivalence with the previous model (one-off, local):** every generated cell was compared
  with the old model's output converted by `percentToTicks`.
  - The same normalised hash came out of 381,444 generator calls: 7 styles × seeds × density ×
    complexity × syncopation × fills × swing {0, 0.25, 0.7} × variation {0.4, 1} × grids
    {16×4, 64×4, 16×1, 32×2}, then Pattern, Variation, Fill, Mutate with locks, and Song.
  - Sensitivity: a one-tick perturbation of one micro value changes the hash.
  - On that basis the R4 generator digests and `factory-content.csv` (patterns and songs; the
    kits are unchanged) were re-pinned.

### Measured state size (§10.5, the worst-case fixture)

The fixture holds 10 patterns at 64 slots × 4 bars and 64 unique 64-slot song bars, every cell
used. It also holds the largest embedded user style (`DSTL`, canonical text 65,459 bytes) and
explicit pad roles (`DROL`). Measured on the saved container:

| Section | 6-byte cell (before option B) | 4-byte cell (now) |
|---|---|---|
| DSEQ | 247,138 | **165,218** |
| DSNG | 395,785 | **264,713** |
| DSTL | (not in the fixture then) | **65,499** |
| Others (DPRM, DPAD, DSMP, DGEN, DUIS, UIST, DROL) | 5,077 | 5,098 |
| Whole state, without samples | 648,088 | **500,632** |

- Reserve: 736 KiB, unchanged.
- With 166 KiB of sample references: **83,048 bytes to spare**.
- The unknown-section boundary tests (at the budget, one byte over) pass unchanged.

### Tests

- **Kit engine:**
  - the step rules on the cell model;
  - triplet cells, fractional ratchets rounded once, tuplets not swung;
  - the scheduler equals the canonical list with random straight/triplet masks and tick micro
    at 44.1/48/96 kHz × 64/333/512/4096 blocks;
  - the song scheduler;
  - host bars: host 4/4 = counted, host 3/4 cuts a 4/4 bar, a 3/4 pattern rests in 4/4,
    off-grid host bar lines and indices, a meter change, half a tick of jitter vs a two-tick
    jump.
  - Mutation check (local): removing the cut or the jitter threshold fails these tests.
- **DRM-00 kit parity and legacy parity:** unchanged and passing (bit-identical).
- **Swing matrix (kit engine):** swing 0 / 0.21 / 0.5 × {4/4 at 16th/32nd/64th, 3/4, 6/8 at
  16th/32nd} with random tuplet beats, ratchets and micro, at 44.1/48/96 kHz (54
  configurations). Every canonical hit plays once on its sample; no tuplet cell is swung.
- **Cell codec (product):**
  - golden words (empty cell; every flag + ratchet 8 + velocity 127 + micro +240; probability 1
    + micro −240; an edited off cell; locked + ratchet 5 + micro +120);
  - little-endian byte order;
  - round trips of every ratchet × flags × micro −256..255;
  - reserved bits rejected.
- **Product:**
  - lossless 16 ↔ 64 conversions;
  - a collision rejected atomically;
  - an off-grid note kept at its tick (+120);
  - every `DSEQ` v4 rejection: version 3, meter, resolution, mask, a reserved slot (on, or off
    with values), reserved bits 29/31, probability 0/101/127, micro ±121, +255 and −256,
    truncation, a trailing byte;
  - a development `DSEQ` v1 is rejected unchanged.

