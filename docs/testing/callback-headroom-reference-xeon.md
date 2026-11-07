# Callback headroom on the reference CPU (Xeon E5-2680 v4)

> **Reference hardware measurement only.** This page is **not** a v1.0
> hardware requirement, **not** a supported-configuration list and **not** a
> product-wide realtime guarantee. It records one controlled A/B measurement
> on one machine (owner disposition, #109 comment 6703361017, item 3).

Capacity and realtime are separate properties:

- **Capacity:** the 1.0 contract maxima stay 16 slots, 2048 MIDI events per
  block and 128 host parameter endpoints × 16 slots. In every measured cell
  below, all accepted events were processed. There were 0 dropped MIDI
  events, 0 audio-thread allocations, and output was finite and non-silent
  with the B2 outputs active.
- **Realtime:** whether each block meets its deadline depends on the
  hardware and the workload. The table shows where it did on this machine,
  and where it did not.

Nothing on this page narrows the contract or the workloads. Today an overload
makes a block late; no events, voices or automation are shed. Any overload
policy or product-wide guarantee needs a separate owner decision.

## Environment

| | |
|---|---|
| CPU | Intel Xeon E5-2680 v4 @ 2.40 GHz, 14 cores / 28 logical, max 2401 MHz |
| OS | Windows 11 Pro 10.0.26100 |
| Power plan | Balanced (381b4222-f694-41f0-9685-ff5bb260df2e) |
| Build | MSVC (Visual Studio 2022), Release, identical CMake configuration for both sides |
| Runner | self-hosted runner `1`. A second runner shares the host, but no other job worker ran during any pass |
| Background CPU | 17.4 / 12 / 9.6 / 10.7 / 10.7 / 5.2 / 5.3 / 6.4 % (6 s average before and after each pass) |
| Run | [42175682645](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/42175682645), job `same-runner-ab` |
| Raw data | artifact `Vox-Callback-Headroom-AB` of that run: `pass1-A.md`, `pass2-B.md`, `pass3-A.md`, `pass4-B.md`, `environment.txt`, `drums-render-equivalence.txt` |

| Side | Commit | Drums DLL SHA-256 | `VoxFullOnDrums.vins` SHA-256 |
|---|---|---|---|
| A, baseline (before the Drums DSP optimisation) | `15456c69b094af7f10efbaac297ae1d469ada1df` | `85a384ee07f62d384a7d80f06e0afa758b2b52907b7f4c608d9db22c725cee3b` | `30a97c8ac062402a46243bf8a66273c94adc126cfcf2b61d575f8ad477847505` |
| B, candidate | `60984bbbc9891848c7f599e98ce9a4641440934a` | `fcc81eb90a2c40ab683c0934576b1e7ab334d15f9f5c2509f0ec73c52661166f` | `d23307df7959e8c12f9141d53b5ab7ecb7d0148b5d199050f8963e59747d17ef` |

The benchmark and the Rack source are identical between A and B; only the
Drums module differs. The bench executables have different hashes because
they were built in different directories.

## Method

- One job builds both commits and runs the full `callback_headroom_bench`
  matrix four times, in the order A, B, A, B:
  - 44.1/48/96 kHz × 64/128/512 frames;
  - `representative` and `declared-limit` (2048 events + 128×16);
  - Drums x1/x2/x8/x16, each at typical load and at the declared limit;
  - declared-limit repeats at 96 kHz.
- Each cell runs for a warmup and then 3–4 s of audio. Each block's wall time
  is compared with its budget (block / rate).
- Late blocks are classified as work, preempted or unknown, from per-block
  thread CPU calibrated against an independent clock. The calibration is
  valid only when both halves of a run agree within 5% (see
  `docs/testing/callback-headroom-20261105.md`).
- **Pairwise comparison:** within each build the first pass was slower than
  the second (A1 > A2, B1 > B2). Compare A1 with B1 and A2 with B2, never A1
  with B2.
- **Cell labels** below come from the candidate's passes 2 and 4:

| Label | Meaning |
|---|---|
| **OK** | 0 late blocks in both passes and p99 load ≤ 80% |
| **marginal** | late blocks ≤ 0.5% |
| **over** | late blocks > 0.5% while p50 is under budget |
| **FAIL** | p50 ≥ budget: a deterministic miss |

## Result (candidate B, reference CPU only)

| workload | 44.1/64 | 44.1/128 | 44.1/512 | 48/64 | 48/128 | 48/512 | 96/64 | 96/128 | 96/512 |
|---|---|---|---|---|---|---|---|---|---|
| representative (mixed) | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| declared-limit (mixed, 16 slots) | OK | OK | OK | marginal | OK | OK | over | over | marginal |
| drums x1 / x2 | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| drums x1 limit | OK | OK | OK | OK | OK | OK | OK | OK | OK |
| drums x2 limit | OK | OK | OK | OK | OK | OK | marginal | marginal | OK |
| drums x8 | OK | OK | OK | OK | OK | OK | marginal | marginal | OK |
| drums x8 limit | over | over | marginal | over | over | OK | **FAIL** | **FAIL** | over |
| drums x16 | marginal | OK | OK | over | marginal | OK | over | over | over |
| drums x16 limit | over | over | marginal | over | over | marginal | **FAIL** | **FAIL** | **FAIL** |

## Deterministic missed deadlines (FAIL)

| Case | Budget | p50 A1 / A2 (µs) | p50 B1 / B2 (µs) | Late blocks B1 / B2 |
|---|---|---|---|---|
| 96 kHz / 64, drums x8 limit | 667 µs | 880 / 865 | 808 / 792 | 4500 / 4500 of 4500 |
| 96 kHz / 128, drums x8 limit | 1333 µs | 1418 / 1403 | 1443 / 1418 | 2249 / 1952 of 2250 |
| 96 kHz / 64, drums x16 limit | 667 µs | 1515 / 1476 | 1433 / 1369 | 4500 / 4500 of 4500 |
| 96 kHz / 128, drums x16 limit | 1333 µs | 2508 / 2320 | 2266 / 2189 | 2250 / 2250 of 2250 |
| 96 kHz / 512, drums x16 limit | 5333 µs | 8026 / 7230 | 6504 / 6265 | 562 / 562 of 562 |

The `drums xN limit` stimulus is the declared capacity: 2048 events per
block in Both mode, spread over N Drums slots, plus 128×16 automation. At x16
that is 128 events per slot per block, about 2 per sample at 64 frames. It is
a capacity stress, not a musical density. The musical-density workloads (E2,
`callback_headroom_bench --music`) are measured separately and are **not**
part of this page.

## Caveats on validity

- **Calibration:** invalid in 65 of 384 cells, all of them light (non-limit)
  workloads at 128/512 frames, where a half-run has too little CPU time to
  calibrate. Those cells had 0 late blocks. Their tails are
  **inconclusive**, not passes.
- **Tails:** p99, p99.9 and max are affected by host activity even on an idle
  host. The unchanged `declared-limit` 96 kHz/64 workload varied between 19
  and 285 late blocks out of 6000 across repeats. Only the FAIL rows,
  deterministic at p50, are robust against this.
- **Power plan:** Balanced, not High performance. Results on other plans,
  CPUs or Windows builds are unknown.
- **One machine:** a 2016 Broadwell-EP server CPU at 2.4 GHz with no
  per-core turbo data. Nothing here predicts results on desktop DAW CPUs; S2
  (owner DAW machine) is the pending measurement for that.
- **Synthetic stimulus:** Drums load comes from synthetic stimuli (the bench
  configuration in `tests/performance/CallbackHeadroomBench.cpp`), not from
  approved factory kits.
- **Not tested:** no DAW was involved. Host scheduling, plugin wrapper
  overhead and driver behaviour are not represented.
