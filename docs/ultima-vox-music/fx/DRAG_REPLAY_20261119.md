# VOX FX — reuse a drag snapshot without duplicating files

Follow-up to [TL review 7031774119](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7031774119) of `4c31369c33f46f39ee14d5bf7664169edc666139`. **Development only / Draft / HOLD / NO MERGE.** No intermediate Windows job or owner DAW run.

## Workflow and lifetime

The same prepared WAV or MIDI snapshot becomes ready again when a native drag ends. To retry a canceled or unaccepted drop, drag the handle again; that does not start a Save chooser, schedule a worker or create a new temp path. **Prepare** again only when a new captured snapshot is wanted. At most one launch per pointer gesture and one native operation per processor; a completion carries that operation's ticket. A synchronous completion cannot reenter its launcher, and an old or duplicate completion cannot unlock a later drag of the same path. The callback still owns only a file lease. State, the 19 host parameter IDs, audio rendering and publication paths are unchanged.

The warning **Copy into project · Save for a lasting file** is visible by the handle. A prepared but unoffered file is still cleaned up by FX; files already offered remain in OS temp even if a drop was canceled, until the OS or the user removes them. The revised retry path avoids creating another file, but deliberately does not cap or time-evict newly prepared snapshots that have been offered. FX cannot determine a host's last reference or safely identify abandoned files. There is no automatic or opt-in scratch cleanup in this checkpoint. Save to a durable path or enable host copy into project. Windows OLE lifetime, focus/multi-monitor/DPI, target acceptance, copy-vs-reference behavior and unload are still unverified. The existing-target Save overwrite identity limitation is unchanged.

## Native evidence

Product `0eef3037d2b835742f43800ec3fa7c72a4457120`; additional regression `3ac347581ed024e32583e2a35961b0b9e4e10039`. The pinned JUCE Debian/GCC 12.2 Linux [offscreen processor and file export evidence](evidence/drag-replay-native-20261119.txt) passed with zero failures, including seven sample-exact capture comparisons and the cross-device publication fixture. The processor fixture checks same-path replay after editor close, a stale completion during a later drag, native rejection after prior handoff, synchronous completion/reentry, and no new Save, worker, state, or path on replay.

## FX-T18 — retain both runs

| Linux run | Callbacks | Wall misses | Thread-CPU misses |
|---|---:|---:|---:|
| [First routed run overlapping catalog rendering](evidence/drag-replay-rt-concurrent-render-20261119.txt) | 8,640 | 4 | 1 |
| [Routed repeat after the render ended](evidence/drag-replay-rt-routed-20261119.txt) | 8,640 | 1 | 0 |
| [Authored repeat](evidence/drag-replay-rt-authored-20261119.txt) | 8,640 | 0 | 0 |
| [Typical repeat](evidence/drag-replay-rt-typical-20261119.txt) | 1,440 | 0 | 0 |

All recorded pairs were valid; the Linux probes recorded zero scoped allocations and zero wrapped locks. The CPU miss in the first run was **96 kHz / 64 / 1 with Pattern**: **706.42 µs CPU**, **1486.88 µs wall**, **666.67 µs deadline** on the same callback. The repeat still recorded a **44.1 kHz / 512 / 8 with Pattern** wall miss: **20,057.99 µs wall**, **2060.25 µs CPU**, **11,609.98 µs deadline**. Run conditions differ; the overlap does not establish a cause, and the repeat does not clear any earlier miss. **FX-T18 remains OPEN.**

Use the [candidate gate](CANDIDATE_GATE_20261119.md) for the one explicitly authorized exact-head Windows Release and later owner Cubase/REAPER qualification. Separately, [Session Cuts](WAVE_B_SESSION_CUTS_20261119.md) adds 24 review-only cues without a new ZIP; no private listening delivery or approval is implied.
