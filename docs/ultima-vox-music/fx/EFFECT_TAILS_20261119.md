# FX-04 tails — measured against the declared host tail

**Draft / HOLD / NO MERGE.** Measurement plus one regression test; no DSP change and no new IDs. Raw rows: [effect-tails](evidence/effect-tails-20261119.txt).

## What is asserted

`Processor::getTailLengthSeconds()` returns **41 s**. Over-declaring is safe — the host simply allows more time — but an unbounded chain is not: it keeps delay/reverb memory and CPU alive across events and can smear the next trigger. The new `tests/suite/FxEffectTailTests.cpp` (CTest `suite_fx_effect_tail_tests`, label `suite`) feeds one 50 ms burst through worst-case serial chains and requires:

1. the case is audible;
2. the −90 dBFS audible tail is shorter than the declared 41 s;
3. the last nonzero sample arrives inside the declared tail;
4. the final second of the render is digitally silent, and a silent input chain stays silent from the start.

## Measured (44.1/48/96 kHz agree within 4 ms)

| Chain, max amount | Audible tail (−90 dBFS) | Last nonzero | Peak | Final second |
|---|---|---|---|---|
| Reverb | 0.353 s | 1.337 s | 0.4477 | 0 |
| Tempo Delay, 30 BPM, max feedback | 10.050 s | 10.050 s | 0.5000 | 0 |
| Free Delay, max feedback | 10.050 s | 10.050 s | 0.5000 | 0 |
| Reverb → Tempo Delay → Reverb → Drive | **11.244 s** | 12.213 s | 0.3622 | 0 |
| Four reverbs | 0.667 s | 1.853 s | 0.3321 | 0 |

The worst measured chain, Run‑around reverb/delay, stops writing nonzero audio at **12.2 s** — well inside the 41 s declaration and close to the 10 s delay retirement cap plus the reverb retirement window. Retirement is logical (buffers are not cleared on the audio thread), so nothing is allocated or cleared during the tail; the output simply becomes exactly zero.

## Why this rather than re-tuning

The declared 41 s is deliberately conservative and stays as it is: changing a host-visible tail length is a plugin-contract change, and the measured worst case already ends four times earlier. The risk being closed is silent regression — a future reverb or delay change that makes the chain louder for longer — and that is what the new executable check catches.

**Not qualified:** host-side tail application on Stop/offline bounce, Windows behaviour, and listening judgement of reverb/delay decays on musical material.

## Verified at `418b113`

- **51/51 Linux executables** pass at `418b113ce8c25e4047b8076db87e19962af693c5`: [48 standalone](evidence/effect-tails-inventory-20261119.txt) including the new tail test, plus the [three pinned-JUCE native](evidence/effect-tails-native-20261119.txt) harnesses (0 processor failures, 0 runtime-stress failures, unchanged export hashes).
- [ASan/UBSan](evidence/effect-tails-sanitizers-20261119.txt) passed nine executables, the tail test included.
