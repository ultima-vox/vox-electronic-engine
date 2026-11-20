# VOX FX — reversible Motion and Pattern authoring

Development follow-up to the [TL review of `459b264`](https://github.com/ultima-vox/vox-electronic-engine/pull/156#issuecomment-7032349625). **Draft #156 / HOLD / NO MERGE.** This closes two authoring gaps, not the Windows or owner qualification gate.

## Motion: undo or redo a committed edit

In **MOTION**, adding/replacing/deleting a route, applying the LFO or sample-and-hold clocks, and applying/resetting the event step/ADSR shape each become one undo entry. Use the footer buttons or Ctrl/Cmd-Z, Ctrl/Cmd-Shift-Z or Ctrl/Cmd-Y when the Motion panel handles the key. Unapplied controls are drafts, not history entries. An unchanged Apply does not publish. A new edit after Undo drops Redo. The journal holds at most 32 committed actions and is editor-local; it is not part of a DAW project or preset. Undo only republishes Motion; it does not reset host macro values, the Program or Pattern.

The processor checks the expected Motion config and a writer-side revision in the same transaction. An external Motion edit, even one that later returns to identical values, or a whole Program/project restore invalidates the old journal. Stale Apply/Undo cannot overwrite that state. This does not change the audio callback's lock-free read path. Case-specific host keyboard focus and screen-reader behavior remain to be checked on Windows.

## Pattern: vary the selected bar, not the whole loop

In **Pattern → Edit…**, choose **Shift bar one step earlier**, **Shift bar one step later**, or **Reverse bar step order**. Each command moves complete step records (trigger, note, velocity, probability and ratchets) only inside the selected bar across all of its display pages. Shifts wrap at the bar edge. Reverse changes the order of steps, not the direction of audio. Other bars, dormant steps, grid, swing and probability seed are unchanged; because the scheduler draws probability by position, a moved step can get a different draw. A changed bar is one Undo/Redo entry. A stale command aimed at the pre-restore bar is rejected rather than applied to the restored Pattern.

Host IDs, FMOD/FPAT encoding, playback scheduling, sound rendering and file-transfer behavior are unchanged. Actual Pattern MIDI import, OLE, save/dialog, bounce and unloading remain in the [candidate gate](CANDIDATE_GATE_20261119.md). The [200-cue listening matrix](evidence/fx-review-200-working-matrix.tsv) is unchanged; no new audio package, upload, or approval.

## Save overwrite decision still needed

No reviewed native primitive here atomically says “replace this destination only if it still has the observed identity.” `MoveFileExW` and `ReplaceFileW` operate on destination names; a destination handle that denies delete also prevents the normal replacement. The precommit observation therefore remains a **best-effort guard**, not compare-and-replace. Before release the owner must explicitly accept the remaining race for the overwrite workflow, or require a create-only/different-filename policy for Save. Do not infer acceptance from the TL's development integration. Do not extend the guarantee to network/cloud paths.

## Local evidence

Source `446e05dc140069a6a20ebf9762011f2f8df78c8b`. [Native transcript](evidence/motion-pattern-authoring-native-20261119.txt): Debian x86-64/GCC 12.2, pinned JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, `VOX_FX_TIMING_THREAD_CPU=1 taskset -c 0 bash docs/ultima-vox-music/fx/evidence/run-linux-native-fx.sh build/fx-converge/native`. Native processor, file export and routed stress exited zero; seven WAV parity cases were sample-exact and the file test exercised cross-device refusal. Offscreen tests cover route, clock and shape history, redo branching, stale Motion revisions, multi-page triplet shifts and stale Pattern restore. [Focused transcript](evidence/motion-pattern-authoring-focused-20261119.txt): seven optimized suites passed; Pattern Grid and ProgramStore also passed ASan/UBSan. All eight grids were exercised for variation and round-trip SMF equality.

The single routed diagnostic recorded 8,640 valid wall/thread-CPU pairs, zero zero-deltas, zero measured misses, and zero scoped allocations/wrapped locks. This was not a three-profile loaded qualification and does not clear the prior 706.42 µs thread-CPU overrun against 666.67 µs at 96 kHz/64 frames with Pattern. **FX-T18 remains open.** No Windows job was requested or opted into.

Follow-up `38fd7662146bf229cc2d3ca611d5e829781fe568`: the [offscreen native regression](evidence/motion-history-boundaries-native-20261119.txt) passed with 35 clock commits, exactly 32 undo/redo transitions, rejection of Undo across a Pattern-only project restore with unchanged Motion bytes, and ordered replay of mixed shape/clock edits. The existing external edit-and-revert test still passes. This tests the Motion editor's commands and processor state, not child focus in a DAW. At the Windows GUI gate, check editor focus, route list, numeric-text entry and host focus separately; host shortcuts must not be intercepted outside Motion.
