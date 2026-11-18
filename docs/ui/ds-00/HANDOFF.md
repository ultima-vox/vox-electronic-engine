# Handoff and implementation order

This file does not open the implementation. Visual owner sign-off on #127 is still pending. Neon concept art is not the direction (owner decision 2026-11-18). Hardware reference #135 remains visual-only.

## Order

1. **Shared primitives, separate PR, still no Drums rewrite.** `VoxFader` and a meter drawing that takes a caller-supplied peak. Keep `VoxKnob` for rotary parameters. Do not add `VoxKeyboard` for Drums.
2. **Binding fixes that do not require the new layout, only if TL schedules them.** Split pad select from `audition`. Relabel `drm.kit.volume`. These touch `products/drums` and are out of scope for a docs PR.
3. **Drums dock editor, after visual sign-off, its own draft PR.** One Pad Rack, Inspector, one or two central editors, one bottom mixer, named layouts. Rebind the 174 IDs. Delete eight primary pages from the navigation. Do not change ParameterIDs, DSP, or VST3 identity.
4. **Telemetry or honest gaps.** Velocity snapshot and per-bus OVER are processor work. Until they exist, the UI shows binary hit flash and no CLIP lamp.
5. **FX UI** follows FX DSP/controller on the arena branch. Do not fork `libs/vox-ui` there. Do not describe eight workspaces as present. Re-read the FX SHA at implementation time.
6. **Synth** is independent. No shared host, no Rack shell.

## Non-goals

- WebView/HTML in the VST3.
- Copy of MPC, Maschine, Groove Agent, Serum, Vital, or the rejected neon sheet.
- Fake CPU, voice, grain, or clip indicators.
- Main-only gain under the Kit Volume ID.
- Insert-MIDI-to-track.
- Merging this documentation into `main`.
- Treating `next/standalone-suite`'s older `products/drums` as the 174-parameter editor.

## Release gates (later, not this PR)

- Owner visual sign-off.
- Windows 10/11 x64 CI on the implementation SHA.
- Steinberg validator, VST3 package, SHA-256.
- 44.1/48/96 kHz and 64/128/512 frames.
- One owner DAW pass on Cubase and REAPER.
- No open P0/P1 and no control whose label lies about the parameter.

## For TL review

- [ ] Base is `next/standalone-suite` `f1d6eb59`. Drums facts cite `ef13daa3`, not the stale drums tree on the base (`parameters.csv` 6249 vs 8880 bytes).
- [ ] FX facts cite `a43c46e4`, not `685241ad` from the issue text.
- [ ] Matrix has 174 `HOST-drm.*` rows, unique `row_id`, status only `existing|partial|absent|proposed`.
- [ ] `VoxComponents.h` list matches the header.
- [ ] Kit Volume label bug and select/audition collision are written as defects, not as the target design.
- [ ] No C++ diff, no images, no merge.

## Choices the TL should check

1. **Macro tooltip vs CSV (FX).** `FxEditor.cpp` tooltip says the macro center is neutral. `fx.macro.N` in CSV is float 0..1 default 0.5. Which mapping is intended?
2. **1440×1080 token vs 1040×700 #127 minimum.** Which number is the layout reference for the future dock?
3. **Where copper lives.** #127 wants a Drums action colour distinct from cyan focus. It is not in `Tokens.h`. Should it be a product accent only, or a named token?
4. **`ui.page` vs a new layout blob.** #127 says do not keep the eight-page contract for users. Whether `ui.page` remains as unused compatibility is a #130 question. This matrix does not decide it.
5. **Who calls `takeHitMask` / `takePadMeter` / `takeMainMeter`.** They consume on read. A shared `VoxMeter` must not call them. Confirm the processor still wants that consume-on-read contract.
6. **FX include path.** `FxEditor.h` includes `"components/VoxKnob.h"` and names `vox::ui::VoxKnob`. CMake was not re-read. Confirm it is `libs/vox-ui` and not a fork.
7. **Song copy-on-write UI.** Marked partial/UNVERIFIED. The segmented Edit Source / Edit This Bar control from #127 was not found. Confirm before a dock PR rewrites song editing.
