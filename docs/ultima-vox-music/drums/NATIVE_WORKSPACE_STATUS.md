# Native Drums workspace — development ledger

Owner: Codex, per [#141 assignment 6986368003](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6986368003),
updated 2026-11-18T17:32:32Z. Starting source:
`integration/vox-drums-1.0@ef13daa3887dbd6079054384b04be7b65849d9ef`.
Continue this work in `dev/drums-native-workspace` and its single Draft PR.
No merge or release is authorized by this ledger.

## Implemented first slice

- `DrumsWorkspace`: persistent host-status bar; bounded left/right docks;
  one/two central Step/Song/Sound/Sample views; draggable dock/split dividers; one bottom
  Mixer with collapsed, compact and expanded sizes. Narrow splits degrade to
  one editor without changing the requested layout.
- One 16-pad Rack switches between 4x4 and scrollable List. Separate SELECT
  and HIT buttons: SELECT changes canonical context without sending a note;
  HIT queues the existing processor audition at velocity 110 without changing
  selection. Queue refusal is visible.
- Inspector follows the selected pad or pins its object identity. Level,
  Tune, Decay, Pan, Mute and Solo use real APVTS attachments. Model, MIDI note
  and requested output use KitStore, including external readback. A selection
  change during a knob drag defers rebinding until that gesture finishes.
- One message-thread coordinator drains all 16 pad peaks, Main L/R and hit
  mask once per tick; Rack and Mixer receive the same snapshot. Selection
  and painting never drain meters. Hit activity is binary; the existing mask
  does not provide actual MIDI velocity and no velocity telemetry is invented.
- Sound/Sample share `PadControlBindings.h` with the donor editor: the same
  ranges, choices, setters and value units. All five Sound host controls and
  all structural/sample controls are retained. Sample A/B, async Load, Clear,
  Library and file drop use the actual SampleLibrary. Native waveform trim
  handles retain their captured pad/player while selection changes. Missing
  files retain the previous assignment; completion after closing a view is safe.
- The single Mixer has all 16 strips through horizontal scrolling, vertical
  level faders, rotary pan/sends, mute/solo, requested-output/fallback labels,
  actual pad/Main peaks and the existing Kit Level control. Kit Level retains
  its pre-routing/all-outputs/new-triggers meaning. Compact mode hides sends;
  expanded mode exposes them.
- Reuses `libs/vox-ui` painting and controls without changing its API. Reuses
  the real PatternEditor and SongStrip with scrollable adapters that preserve
  their complete existing toolbars. There is no browser or parallel DSP.

## Evidence and limits

- The standalone model test first failed to compile because the new model
  header did not exist, then passed after implementation. It covers pinned
  object selection, invalid pad indices, all split/Mixer combinations, bounds,
  overlap and narrow-window fallback.
- Native tests exercise real processor audition/audio peaks, APVTS write and
  readback, one balanced host gesture, selection during drag, KitStore binding,
  one-consumer telemetry and unchanged project bytes under layout/rendering.
  Sound/Sample controls are compared against the real donor controls for
  range/step/units/choices, then exercised on a different pad and player B.
  The external model-change test reproduced stale kick-only visibility and
  passed after refresh was fixed. Tests decode a real WAV, drag trim across a
  context change, reject a missing file without losing the buffer/reference,
  and complete a pending load after closing the view. The GUI test owns an
  event dispatcher so queued JUCE UI work can run between test operations;
  production modal-loop configuration is unchanged.
- Content sizes 1040x600, 1280x620, 1440x800 and 1600x900 correspond to the
  required outer sizes after the current 72/28 frame. Tests render Grid/List
  at 1/1.25/1.5/2 raster scales; this is **not** proof of host/OS DPI behavior.
- Optional `VOX_WORKSPACE_SCREENSHOTS` writes replaceable PNG evidence from
  the actual native component. Screenshots are not owner/DAW acceptance.
- Linux uses GNU C++ 14.2, C++20 and verified JUCE 9.0.1 commit
  `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`. Windows qualification belongs
  to the exact published feature head and must be recorded in its PR.
- Local targeted regression: 5/5 (model, native workspace, existing product,
  candidate and samples), followed by 2/2 workspace tests after additional
  pending-load lifetime and GUI-dispatch checks. Debug logs also report JUCE's
  Linux 128-message wake queue assertions during bulk UI construction and
  three static AudioFormatManager shutdown detector reports. These are not
  Windows or DSP results; the inherited product GUI tests also emit queue
  assertions. Functional exit codes passed; do not call these logs warning-free.

This is a **candidate component under qualification**, not a claim that #127
is complete. The production `createEditor()` still uses DrumsPanel until its
entire functional inventory has been ported. Existing production and regression
tests remain intact. No audio, state schema, frozen identity or host parameter
ID was changed. This branch does not include unmerged StyleLibrary #153;
its Windows results are separate evidence, not results for this native branch.

## Continue in the same PR

1. Finish Sound/Sample presentation and sample-specific Inspector contexts;
   preserve every structural/sample setting and existing undo behavior.
2. Complete Generator/roles, Library/Presets, Routing and Diagnostics tools,
   responsive central toolbars and Inspector contexts for steps/Song/sample.
3. Implement the bounded canonical layout/state contract from #130 section 14
   and #127: project layout, per-user workspaces and ephemeral degradation stay
   separate. Do not add DUIS migration/dual-write shims. Shared editor-size/API
   changes require coordination with #156 before editing the common baseline.
4. Switch production editor only after all existing functions are reachable in
   the new workspace; replace the obsolete eight-page visual assertion with
   stronger native workspace acceptance while retaining its functional tests.
5. Run exact-head Windows full build/CTest/validator, actual OS/host DPI at
   100/125/150/200%, all four sizes, state/undo/Song/MIDI/9-output/RT gates.
   Integrate only through TL decision; one final owner Cubase/REAPER pass.

Visual Gate 1 is already accepted in #127 comment 6980638479; do not reopen
browser polishing or request another visual approval loop. Arena owns #156 FX
and #160 browser work. Other release gates (factory licences/listening,
96 kHz headroom and final combined candidate) remain in #141/#152.
