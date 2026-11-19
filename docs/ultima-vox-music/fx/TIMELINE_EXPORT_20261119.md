# VOX FX — timeline authoring and WAV job lifetime

Source: `fce28335712352783c9385ef136c67553ad88c76`. Development checkpoint on top of the accepted Draft #156 head `8c6c672`. State encodings, factory identities, all 19 host parameter IDs and the opt-in Windows CI gate are unchanged.

## Timeline

All 20 trajectory editors share the same bounded horizontal viewport. Roll the wheel over the graph to zoom about the pointer; Shift-wheel or horizontal scrolling pans. Keyboard `+`/`-` zoom at center, `Alt`+Left/Right pans, `0` fits the whole curve. Zoom is bounded to 16× and stored only in the editor instance. Nodes, selection, snap and the rendered DSP cursor all use a shared normalized-to-view transform; slot identity, serialized times and existing undo/group editing are unchanged. Alt-click/drag bypasses normalized 1/16 snap. The axis is the **authored** duration (seconds from the host base or programmed bars, multiplied by layer span for a layer plot), not a fabricated prediction of the remaining time to the next host bar line. Cursor position remains the rendered phase rather than a UI timer estimate. Current Windows resize/DPI/accessibility work is not qualified by these offscreen tests.

## Export lifecycle

Once the user confirms a WAV destination, the processor instance owns the single WAV job, and the worker receives only the immutable capture and file path. Closing the editor no longer cancels or joins a live WAV render or disk write. Reopening resumes progress and can cancel or consume one completion. Jobs are still joined on plugin destruction to prevent code running after unload; the existing temporary-file/write/replace safety contract remains. Preset browser/index lifetime is a separate path, not changed here.

## Test evidence

- [Pinned JUCE processor log](evidence/timeline-export-processor-20261119.txt): 0 failures at 1 MiB stack. Exercises zoom/pan/fit and zoomed node insertion/undo, seconds→bars axis automation and project recall, WAV export across editor close/reopen, duplicate-job rejection, and byte parity despite editing the Program after capture. Seven `processBlock` vs float32 WAV cases retain zero error. New zoom math tests also passed for boundary clamping and nonfinite gestures.
- `FxRuntimeStressTests`, `FxFileExportTests`, and seven focused standalone suites passed: curve trajectory, six playback modes, Program store/codec, bar events, timeline, offline render. ASan+UBSan passed curve trajectory and playback mode suites.
- Fresh Linux/KVM callback smoke with the pinned harness and `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0`: 54 stress cases (8640 callbacks at 44.1/48/96 kHz × 64/128/512 × 1/4/8 with/without Pattern) and nine typical cases (1440 callbacks). Zero wall/CPU misses, scoped allocations and wrapped locks in this run. Stress worst wall occupied 48.88% of its case's deadline (5213.64 µs at 48 kHz/512, 741.30 µs CPU for that callback), worst relative wall p99 39.46%, worst relative CPU max 45.16%. Typical worst relative wall max 6.12% (40.81 µs against 666.67 µs). These are fresh diagnostics, not regression thresholds or FX-T18 qualification. Previous wall-only misses remain valid evidence. [Stress raw](evidence/timeline-export-rt-stress-20261119.txt) · [Typical raw](evidence/timeline-export-rt-typical-20261119.txt).
- [Wave B 32 counterpoints](WAVE_B_COUNTERPOINTS_20261119.md) includes eight added, independently authored cues, two Loop entries, 96 multirate checks and eight synthetic A/B mixes. Both the earlier 24- and new 32-program ZIPs were reconstructed bit-for-bit. No musical or distribution approval.

**Remaining gates:** other FX feature and workflow coverage, catalog 300/600 and real-mix listening, durable private audio handoff, Windows GUI/host bounce/DPI, Windows x64 FX-T18 and exact-head full suite/validator/binary SHA, then owner Cubase/REAPER. No intermediate Windows CI, no change to Draft #156 or HOLD.
