# State boundaries

Status: **IN PROGRESS / NO MERGE**. This file records the TL decision. It does not change DSP, ParameterIDs, or FX.

Reviewed against Drums `ef13daa3887dbd6079054384b04be7b65849d9ef` and FX `1587dbb4a01f7b01c7a8751562bbe6c57fe6fe82`. Comment: [#158 comment 6972053912](https://github.com/ultima-vox/vox-electronic-engine/issues/158#issuecomment-6972053912).

## What `status` means

In [UI_BINDING_MATRIX.csv](UI_BINDING_MATRIX.csv), `status` is UI coverage. `partial` on all 174 `HOST-drm.*` rows means the #127 surface is missing. It does not mean the engine drops the parameter.

`dsp_path=confirmed-static` means the cited source reads the value. DS-00 did not render audio. That column is not a PASS.

## Drums parameters stay as they are

`DrumsProcessor` constructs:

```text
parameters_ (*this, nullptr, "VOXDRUMS", createParameterLayout())
```

`createParameterLayout()` in `DrumsParameters.cpp` is the host registry: 174 IDs, the ranges in `range_code`, and `ParameterID` version 1. A later JUCE editor attaches to those same `RangedAudioParameter` objects. It must not create a second parameter with the same job, and the audio thread must not call APVTS or attachment methods. The audio thread keeps the `std::atomic<float>*` from `getRawParameterValue`.

`nullptr` is the `UndoManager`. Do not replace it with a global undo manager without a separate migration project. For the first release the histories stay apart:

| Gesture | Where it lives | Not the same history as |
|---|---|---|
| Host automation and preset parameter values | APVTS, serialized as plain values in section `DPRM` | Pattern undo, kit edits, layout |
| Pattern / song undo | Pattern store (`patternEpoch_` ends that history on restore) | APVTS |
| Structural edits | Kit store via `changeStructure` / `store_.setAll` | APVTS |
| Workspace layout | Not a host parameter. Must not be written into APVTS or `DPRM` | All of the above |

## Three mechanisms

1. **Host automation.** The 174 `drm.*` IDs, and on FX the 19 `fx.*` IDs. Drums uses APVTS. FX uses `addParameter` and keeps `AudioParameterFloat*` / `AudioParameterBool*`. Both are host parameters. FX not using APVTS is not a defect. `SliderParameterAttachment` only needs a `RangedAudioParameter`.
2. **Instrument structure.** `store_.pull(live_)` runs before the host overlay in `processBlock`. Model, note, filter, sample fields, pad `output` (`KitEngine.cpp` sets `v->bus = ps.output`), and `voiceLimit` are this path. They are not host IDs. Sample bytes are the sample library, not APVTS.
3. **Workspace.** Dock, pin, named layouts, and reset are UI state. They are not `ui.page` and they are not parameters. `DUIS` today stores editor UI state; a future layout blob must stay out of the parameter set.

Host values are applied on top of the store every block: one value, one atomic, no second copy inside the widget.

## FX macros

At `1587dbb4`, `fx.macro.N` is `AudioParameterFloat` `0..1`, default `0.5`. `FxEditor.cpp` attaches one horizontal slider to the selected macro only. Wet and amount have their own `SliderParameterAttachment` per slot. `macroDepth_` is `-1..1` and calls `setMotionRoute`; it is not a host ID.

`products/fx/engine/MacroRouting.h` `Runtime::processStereo` maps a finite macro with `2 * clamp(v, 0, 1) - 1`. Stored `0.5` becomes modulation `0`. If `config_.count == 0` the macros are not applied and the base gain is returned. The tooltip "Bipolar macro value: center is neutral" matches that mapped source. It is not a second automation range. Do not change the parameter range to "fix" the tooltip. The slider's text is the host value. `tests/suite/FxMacroRoutingTests.cpp` asserts the neutral case; DS-00 did not run the test. No FX follow-up is opened here.

## Realtime reading

`load(std::memory_order_relaxed)` on one `std::atomic<float>` is the lock-free read used for each host value. It does not build a consistent snapshot of several parameters, and it does not remove every other realtime hazard (allocation, locks, UI calls). `getRawParameterValue` is taken once in the constructor. `takePadMeter`, `takeMainMeter`, and `takeHitMask` are telemetry: they `exchange` to zero and are not parameters. There is still no per-bus clip latch and no velocity in the hit mask. Audition's velocity argument exists; the editor call passes a fixed `110`.

## Out of scope

- Rewriting the parameter routing.
- Moving FX onto APVTS.
- Importing the neon sheet or the HTML colours `#070d13`, `#fa8549`, `#3ce4d4`.
- Treating `hardware-reference.html` as DSP or as a replacement for #127.
- Merging this branch.
