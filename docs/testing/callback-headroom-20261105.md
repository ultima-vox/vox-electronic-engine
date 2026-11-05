# Callback headroom (release-candidate measurement, #77)

Tool: `callback_headroom_bench` (`tests/performance/CallbackHeadroomBench.cpp`).
It runs the real Rack VST3 processor through `processBlock` and times each
block against its realtime budget (block / rate). It does **not** judge
timing, because the numbers depend on the machine. It fails only on:
- non-finite or silent output;
- silent B2 host outputs in the limit workload;
- dropped MIDI at the declared limit;
- an audio-thread allocation (`tests/support/AllocationProbe.h`).

Windows CI runs the full matrix on every build (step *Measure callback
headroom*). It uploads the report as the artifact `Vox-Callback-Headroom`,
with the head commit and the runner CPU. The CTest entry
`callback_headroom_smoke` runs one quick configuration.

## Workloads

| Workload | What runs |
|---|---|
| representative | Full-on Drums: internal sequencer on the factory pattern, a sampled Layer kick, LP 24 and BP hat filters, a clap transient, snare/clap pad sends. Vox Demo Synth: 4-note chords. Graph Demo Synth: a 16th-note bass line. Kick ducking on both synths, returns 1 and 2, slot sends, 6 pool parameters per slot automated every block |
| declared-limit | **16 loaded slots** (Drums ×4, Demo ×6, Graph ×6). **2048 host MIDI events per block** (the declared router limit; NoteOn/NoteOff/CC on all 16 channels). **128 × 16 host automation**: every pool endpoint of every slot changes every block. Returns, ducking, and the **B2 host outputs Out 2 … Out 8 active**, with Drums pads routed there |

Every configuration runs 4 s of audio after a warm-up. The columns are:
- p50 / p95 / p99 / p99.9 / max: `processBlock` time, in µs;
- Load: p99 / budget;
- Blocks over budget: measured blocks slower than realtime.

> **Stimulus correction (review 5777473229, P2).** The tables below at
> af28b12 and dec2057 used a flawed declared-limit stimulus. Each NoteOff
> went to the next slot's channel, and at an unrelated offset, so many
> notes were not released. Since the fix, each NoteOn has its NoteOff on the
> same channel and note, later in the same block. The bench checks this:
> 16 distinct channels, exactly 2048 events, every note balanced. The
> earlier numbers are kept for the record only. The Windows matrix for the
> corrected stimulus is the `Vox-Callback-Headroom` artifact of the commit
> that carries this note.

## Local reference run (Linux container, not the release machine)

Machine: shared cloud container, 4 vCPU Intel Xeon @ 2.10 GHz, GCC 13
RelWithDebInfo build, branch `perf/callback-headroom` on `integration/rack-1.0`
f9c8bc6.

The container is not isolated, so the max and p99.9 columns include
scheduler noise from other tenants. The release numbers are the Windows CI
artifact and, finally, the owner's DAW machine.

| Workload | Rate | Block | Budget µs | p50 µs | p95 µs | p99 µs | p99.9 µs | max µs | Load p99 | Headroom p99 | Blocks over budget | Audio-thread allocations |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| representative | 44.1 kHz | 64 | 1451 | 40.0 | 52.3 | 65.0 | 94.4 | 106.0 | 4.5% | 95.5% | 0 / 2756 | 0 |
| representative | 44.1 kHz | 128 | 2902 | 69.8 | 89.5 | 108.0 | 128.9 | 135.2 | 3.7% | 96.3% | 0 / 1378 | 0 |
| representative | 44.1 kHz | 512 | 11610 | 242.3 | 316.3 | 347.5 | 557.3 | 557.3 | 3.0% | 97.0% | 0 / 344 | 0 |
| representative | 48.0 kHz | 64 | 1333 | 40.6 | 55.1 | 70.2 | 90.7 | 446.7 | 5.3% | 94.7% | 0 / 3000 | 0 |
| representative | 48.0 kHz | 128 | 2667 | 68.4 | 87.8 | 98.0 | 106.7 | 110.6 | 3.7% | 96.3% | 0 / 1500 | 0 |
| representative | 48.0 kHz | 512 | 10667 | 247.7 | 475.8 | 1093.6 | 1984.3 | 1984.3 | 10.3% | 89.7% | 0 / 375 | 0 |
| representative | 96.0 kHz | 64 | 667 | 39.7 | 56.5 | 78.2 | 251.4 | 2093.0 | 11.7% | 88.3% | 3 / 6000 | 0 |
| representative | 96.0 kHz | 128 | 1333 | 74.0 | 116.1 | 143.5 | 365.6 | 854.9 | 10.8% | 89.2% | 0 / 3000 | 0 |
| representative | 96.0 kHz | 512 | 5333 | 251.8 | 320.6 | 362.7 | 758.0 | 758.0 | 6.8% | 93.2% | 0 / 750 | 0 |
| declared-limit | 44.1 kHz | 64 | 1451 | 286.4 | 515.0 | 594.7 | 2464.5 | 2601.3 | 41.0% | 59.0% | 5 / 2756 | 0 |
| declared-limit | 44.1 kHz | 128 | 2902 | 403.6 | 700.7 | 821.0 | 1561.0 | 1568.2 | 28.3% | 71.7% | 0 / 1378 | 0 |
| declared-limit | 44.1 kHz | 512 | 11610 | 1118.5 | 1842.7 | 2145.2 | 2654.6 | 2654.6 | 18.5% | 81.5% | 0 / 344 | 0 |
| declared-limit | 48.0 kHz | 64 | 1333 | 282.6 | 503.2 | 600.5 | 1538.5 | 3842.8 | 45.0% | 55.0% | 4 / 3000 | 0 |
| declared-limit | 48.0 kHz | 128 | 2667 | 413.5 | 665.1 | 791.0 | 1877.6 | 1957.0 | 29.7% | 70.3% | 0 / 1500 | 0 |
| declared-limit | 48.0 kHz | 512 | 10667 | 1085.1 | 1611.8 | 3310.6 | 4391.8 | 4391.8 | 31.0% | 69.0% | 0 / 375 | 0 |
| declared-limit | 96.0 kHz | 64 | 667 | 277.9 | 502.5 | 569.0 | 964.2 | 7197.4 | 85.3% | 14.7% | 23 / 6000 | 0 |
| declared-limit | 96.0 kHz | 128 | 1333 | 400.2 | 713.4 | 800.8 | 1599.5 | 2290.5 | 60.1% | 39.9% | 6 / 3000 | 0 |
| declared-limit | 96.0 kHz | 512 | 5333 | 1088.2 | 1375.2 | 1888.7 | 2386.3 | 2386.3 | 35.4% | 64.6% | 0 / 750 | 0 |

### Reading (container)
- **Representative workload:**
  - p99 load is at most 12% at every rate and block; it is highest at 96 kHz with 64 or 128 samples;
  - 0 blocks over budget, apart from 3 of 6000 at 96 kHz/64, with a max of 2.1 ms. That is the scale of a container scheduling stall, not of the p99.9.
- **Declared limit:**
  - p99 load is 18–45% at 44.1 and 48 kHz;
  - at **96 kHz/128 it is 60%**, and at **96 kHz/64 it is 85%**, with 23 of 6000 blocks over budget.
  - The limit sends 2048 events into a 64-sample block, 32 events per sample. That is a stress bound, not a musical load.
  - Whether 96 kHz/64 at the full declared limit is acceptable is an owner decision, once the Windows numbers are in.
  - Options:
    - (a) accept it, as the declared worst case;
    - (b) lower the declared MIDI limit for small blocks;
    - (c) profile the router and modules at that point.
  - It is **not** reported as a defect: there is no Windows evidence yet, and the container is noisy.
- **Allocations:** 0 audio-thread allocations in every configuration.

## Windows CI run (self-hosted runner, Release, exact head af28b12)

[Run 41919802775](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/41919802775), job 126888980695. The report with the runner CPU is the artifact `Vox-Callback-Headroom`.

| Workload | Rate | Block | Budget µs | p50 µs | p95 µs | p99 µs | p99.9 µs | max µs | Load p99 | Headroom p99 | Blocks over budget | Audio-thread allocations |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| representative | 44.1 kHz | 64 | 1451 | 104.0 | 156.8 | 214.2 | 269.3 | 333.3 | 14.8% | 85.2% | 0 / 2756 | 0 |
| representative | 44.1 kHz | 128 | 2902 | 169.9 | 253.5 | 336.9 | 415.3 | 450.1 | 11.6% | 88.4% | 0 / 1378 | 0 |
| representative | 44.1 kHz | 512 | 11610 | 551.3 | 773.6 | 946.5 | 1143.7 | 1143.7 | 8.2% | 91.8% | 0 / 344 | 0 |
| representative | 48.0 kHz | 64 | 1333 | 103.5 | 156.1 | 210.0 | 273.3 | 487.6 | 15.8% | 84.2% | 0 / 3000 | 0 |
| representative | 48.0 kHz | 128 | 2667 | 167.5 | 253.0 | 324.5 | 437.1 | 458.5 | 12.2% | 87.8% | 0 / 1500 | 0 |
| representative | 48.0 kHz | 512 | 10667 | 552.6 | 758.3 | 875.7 | 1107.3 | 1107.3 | 8.2% | 91.8% | 0 / 375 | 0 |
| representative | 96.0 kHz | 64 | 667 | 103.0 | 151.6 | 197.9 | 244.6 | 418.1 | 29.7% | 70.3% | 0 / 6000 | 0 |
| representative | 96.0 kHz | 128 | 1333 | 165.8 | 244.9 | 314.6 | 373.6 | 379.1 | 23.6% | 76.4% | 0 / 3000 | 0 |
| representative | 96.0 kHz | 512 | 5333 | 554.9 | 767.8 | 913.3 | 1158.8 | 1158.8 | 17.1% | 82.9% | 0 / 750 | 0 |
| declared-limit | 44.1 kHz | 64 | 1451 | 552.0 | 717.1 | 858.8 | 1086.7 | 1135.6 | 59.2% | 40.8% | 0 / 2756 | 0 |
| declared-limit | 44.1 kHz | 128 | 2902 | 807.7 | 1016.0 | 1139.8 | 1549.0 | 1553.6 | 39.3% | 60.7% | 0 / 1378 | 0 |
| declared-limit | 44.1 kHz | 512 | 11610 | 2315.5 | 2698.5 | 2921.9 | 3247.8 | 3247.8 | 25.2% | 74.8% | 0 / 344 | 0 |
| declared-limit | 48.0 kHz | 64 | 1333 | 550.9 | 712.1 | 829.5 | 1044.1 | 1188.2 | 62.2% | 37.8% | 0 / 3000 | 0 |
| declared-limit | 48.0 kHz | 128 | 2667 | 812.0 | 1029.4 | 1189.8 | 1483.3 | 1508.4 | 44.6% | 55.4% | 0 / 1500 | 0 |
| declared-limit | 48.0 kHz | 512 | 10667 | 2275.6 | 2717.5 | 2954.7 | 3349.1 | 3349.1 | 27.7% | 72.3% | 0 / 375 | 0 |
| declared-limit | 96.0 kHz | 64 | 667 | 546.8 | 710.1 | 844.9 | 1054.5 | 1140.5 | 126.7% | -26.7% | 501 / 6000 | 0 |
| declared-limit | 96.0 kHz | 128 | 1333 | 804.9 | 1027.9 | 1274.4 | 1572.8 | 1627.6 | 95.6% | 4.4% | 19 / 3000 | 0 |
| declared-limit | 96.0 kHz | 512 | 5333 | 2287.5 | 2707.7 | 2949.1 | 3376.8 | 3376.8 | 55.3% | 44.7% | 0 / 750 | 0 |

### Reading (Windows)
- **Representative workload:**
  - p99 load ≤ 16% at 44.1 and 48 kHz;
  - 17–30% at 96 kHz;
  - **0 blocks over budget in all 9 configurations**.
- **Declared limit, 44.1 and 48 kHz:** p99 load is 25–62%, with 0 blocks over budget.
- **Declared limit, 96 kHz:**

  | Block | p99 load | Blocks over budget |
  |---|---|---|
  | 512 | 55% | 0 |
  | 128 | **96%** | 19 of 3000 |
  | 64 | **127%** (p50 547 µs against a 667 µs budget) | **501 of 6000** |

  So 96 kHz / 64 does **not** hold the declared limit on this runner.
- **Allocations:** 0 audio-thread allocations everywhere.

### Finding (reproducible): declared MIDI limit at 96 kHz with small blocks
The per-block cost at the limit hardly depends on the block size: p50 is about 550 µs at 64 frames and about 800 µs at 128. The fixed per-block work therefore dominates.

A local attribution run at 96 kHz / 64 (container) removed one part of the workload at a time:

| Variant | p50 µs | p99 µs |
|---|---|---|
| full limit | 275 | 597 |
| without the 2048 MIDI events | 124 | 254 |
| without the 128 × 16 automation | 277 | 612 |
| without both | 112 | 169 |

The **2048 host MIDI events per block** account for most of the cost: about two thirds of the NoteOns start voices across the 16 slots. The automation adds little.

This is a stress bound, not a musical load: 32 events per sample frame. It fails only with small blocks at 96 kHz. Owner decision, options:
- (a) accept it as the declared worst case and document the limit per rate/block;
- (b) lower the declared host MIDI limit for small blocks, e.g. scale it with block length (a contract change);
- (c) profile and optimise the router and voice-start path, then measure again with this bench.

No timing gate was added, and no budget was widened.

## Corrected stimulus, local container (declared-limit rows)

The finding remains with the corrected stimulus. The per-block cost is
higher than before (p50 about 450 µs at 96 kHz / 64), because every note
now goes through its release path.

| Rate | Block | p50 µs | p99 µs | Load p99 | Over budget |
|---|---|---|---|---|---|
| 96 kHz | 64 | 450.9 | 817.2 | 122.6% | 269 / 6000 |
| 96 kHz | 128 | 636.1 | 1237.7 | 92.8% | 18 / 3000 |
| 96 kHz | 512 | 1743.5 | 3445.6 | 64.6% | 0 / 750 |
| 48 kHz | 64 | 453.7 | 832.9 | 62.5% | 2 / 3000 |
| 44.1 kHz | 64 | 453.7 | 890.0 | 61.3% | 3 / 2756 |

## Not covered here
- Real Bass and Acid builds (#70, #92) are not in staging yet. Add them to the
  workloads when they land.
- Cubase and REAPER callback behaviour: their thread priority and buffer
  scheduling. Final acceptance (#77) is on the release binaries.
