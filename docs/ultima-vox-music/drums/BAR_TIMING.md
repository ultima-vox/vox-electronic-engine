# VOX Drums shared bar timing (PR #137 §10, revision 6)

Status:

- **Step 1 implemented** (`products/drums/engine/BarTiming.{h,inl,cpp}`): the pure timing module
  and the lossless-or-reject subdivision switch.
- **Step 2 implemented** (this branch, `drm/timing-integration`): one cell model for storage,
  scheduler, export, song, generator and the old editor, through `BarTiming`. See
  [Step 2](#step-2-implemented) below. **Three TL decisions are open** (swing, tick → sample
  rounding, the state budget); nothing in the step pre-empts them.

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
  - 16 × bars × slotsPerBar cells: u8 flags, velocity, probability, ratchet, i16 microTicks.
  - Strict: grid, mask, every value, |micro| ≤ ½ cell, distinct ratchet subhits, empty reserved
    slots.
- **`DSNG` v2:** unique bars carry grid, seed, 16 masks and their cells in the same layout. There
  is no song meter.

### Playback and export

- **One rule, `kit::cellHits`:**
  - tick = bar line + cell start + microTicks;
  - ratchet subhits through `subhitTick`;
  - probability key: (seed, absolute bar × 64 + slot, lane).
  - Scheduler and both exports use it.
  - The uniform `stepLength`, `stepHits` and `songHits` are removed.
- **Swing (open, PR 140 comment):** a delay of `swing × cell` on straight cells with an odd index
  in the bar's straight grid. Tuplet cells are not swung.
  - Playback adds it unrounded, which keeps DRM-00 bit-identical.
  - Export rounds tick + delay as before (`llround`).
- **Tick → sample (open):** the scheduler keeps the legacy `ceil(x − 1e-6)` rule (DRM-00);
  §10.5a says `floor`.
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

| Section | Before (computed) | Now (measured) |
|---|---|---|
| DSEQ | 204,868 | 247,138 |
| DSNG | 327,808 | 395,785 |
| State without samples | ≈537,840 | **648,088** |

- **The reserve check fails.** The own state may take 583,680 bytes (736 KiB minus 166 KiB for
  sample references).
- The fixture also lacks `DSTL` (up to 65,576 bytes) and `DROL`.
- **Open TL decision** (PR 140 comment):
  - (B) a bit-packed 4-byte cell (29 bits);
  - or (A) a larger reserve.
- The failing check is kept as it is.

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
- **Product:**
  - lossless 16 ↔ 64 conversions;
  - a collision rejected atomically;
  - an off-grid note kept at its tick (+120);
  - every `DSEQ` v4 rejection (version 3, meter, resolution, mask, reserved slot, values,
    micro ±121, truncation);
  - a development `DSEQ` v1 is rejected unchanged.

