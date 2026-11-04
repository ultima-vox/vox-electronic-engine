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

## Not covered here
- Real Bass and Acid builds (#70, #92) are not in staging yet. Add them to the
  workloads when they land.
- Cubase and REAPER callback behaviour: their thread priority and buffer
  scheduling. Final acceptance (#77) is on the release binaries.
