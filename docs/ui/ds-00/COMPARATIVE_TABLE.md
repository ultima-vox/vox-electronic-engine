# Comparative table — #127, #135, shared UI, code

Drums code SHA: `ef13daa3887dbd6079054384b04be7b65849d9ef`.
Shared UI SHA: `f1d6eb595779c973ae5a139270795aae968804cc` (same `VoxComponents.h` / `Tokens.h` blobs on the Drums SHA).
Hardware reference SHA: `74513b1ce4e670d8126ccd78a8ce1b02a4764789`.
FX SHA: `1587dbb4a01f7b01c7a8751562bbe6c57fe6fe82` (re-read; replaces `a43c46e4`).

## How to read status

`partial` in this table and in the CSV is UI coverage. It is not a statement that DSP drops the parameter. Host rows carry `dsp_path=confirmed-static` when `Processor.cpp` or the FX processor reads them. That evidence is a source reading, not a runtime PASS. Ranges in the CSV `domain` column are the layout literals. Float32 print noise from `describeLayout` (`setprecision(9)`) is kept only in `csv_printed_domain`.



## Latest TL ruling (2026-11-18)

Visual direction is not approved. This table compares requirements with code. It does not pick a palette.

| Ruling | What the sources say | What this audit does |
|---|---|---|
| Functional authority | #127 | HTML does not replace #127. |
| HTML role | `hardware-reference.html` @ `74513b1` | Reference for single layout/behaviour ideas only. See [HTML_PROTOTYPE_VS_127.md](HTML_PROTOTYPE_VS_127.md). |
| Neon sheet | Rejected by the owner | Not copied into `Tokens.h`. |
| `Tokens.h` | Current constants, blob `c1c9a2d` | Inventory only. Not a visual sign-off. |
| HTML hexes `#070d13`, `#fa8549`, `#3ce4d4` | In the prototype stylesheet | Not approved. Not merged into tokens. |
| Dock, Pin, layout save, Reset, Select/Audition | Required by #127; missing or collided in code | Left open. See `GAP-*` rows in the CSV. |
| Eight Drums pages | `pageName` / `showPage` on the integration SHA | Do not carry them into the new architecture. |

| Requirement | Authority | Code now | Status | Accept when | Product-specific? |
|---|---|---|---|---|---|
| Dockable hybrid workstation, not eight pages | #127 | `DrumsPanel::pageName` lists PERFORMANCE, SOUND, PATTERN/SONG, MIXER, GENERATE, ROUTING, PRESETS, ADVANCED. `showPage` writes `ui.page`. | partial | Those tabs are not primary navigation. Dock state is separate from `ui.page`. | Drums editor. Shared code may offer a dock host later. |
| Pad select is not audition | #127 | `PadButton::onClick` calls `selectPad(pad)` and `processor_.audition(pad, 110)`. | partial | Name/list/mixer select does not call `audition`. Explicit gesture does. | Drums `PadButton`, currently local to `DrumsEditor.cpp`. |
| Grid 4×4 and List 16, one Pad Rack | #127 | 16 pad buttons on the PERFORMANCE page only. No List mode. | partial | One dock panel, both presentations, same selection. | Drums. A shared `VoxPad` is proposed, not required to own kit logic. |
| Inspector, pin, quick Level/Pan/Tune/Decay | #127 | Those host knobs exist on the SOUND page for the selected pad only. No pin. | partial | Inspector dock uses the same ParameterIDs. | Drums. |
| Bottom mixer, three heights, vertical faders | #127 | MIXER is page 3. `drm.pNN.level` is a `VoxKnob`. | absent | `VoxFader` or a documented linear slider bound to level. Pan and sends stay rotary. | Fader painting can be shared. Strip routing stays Drums. |
| Kit Volume is all outputs, not Main-only | #127, CSV name `Kit Volume` | PERFORMANCE label `Volume`. MIXER label `Main` on `drm.kit.volume`. | partial | On-screen name is Kit Level / All outputs. No new Main gain ID. | Drums. |
| Main L/R meter | #127 | `takeMainMeter(int channel)` and mixer main meters. | partial | Label and tap text match Processor.h. | Drums tap. Shared meter drawing may be reused only with an explicit tap. |
| Pad peak vs hit vs velocity | #127 | `takePadMeter` is post-fader peak consumed by the mixer. `takeHitMask` is 16 bits, no velocity. Audition velocity is fixed at 110. | partial | Do not draw velocity from the hit mask. | Drums processor. Do not pretend FX `outputPeak` is this API. |
| OVER latch, no CLIP badge | #127 | No `OVER` or `CLIP` string in `DrumsPages.cpp`. | absent | Lamp stays unavailable until a real tap exists. | Drums. |
| Enabled outputs vs clip | #127 | `activeOutputs()` drives routing display. | partial | Tests must fail if an enable bit paints as overload. | Drums. |
| Named layouts vs audio presets | #127 | `ui.page` is editor UI state. Presets page is separate. No Beatmaking/Composition/Sound Design/Mixing layout preset. | absent | Layout blob does not write APVTS. | Drums layout. #130 not re-opened here. |
| Hardware material, no WebView | #135, #127 | HTML file is on `design/vox-drums-hardware-reference` only. Release editor is JUCE. `VoxPaint` is flat dark panels, not the hardware reference. | partial | Native paint. HTML is not linked into the plugin. | Shared paint vs Drums layout. |
| Shared controls only: panel, knob, button, combo, tabs, section header | `VoxComponents.h` | Those six includes. No fader, pad, keyboard, meter, dock. | existing | New classes land in a later PR with this matrix updated. | See registry. |
| FX eight workspaces | FX specs, not #127 | `FxEditor.h` says one honest `FxPanel`; other workspaces deferred. | partial | Do not draw unfinished workspaces. | FX product. Not `libs/vox-ui`. |
| FX host parameters | `products/fx/parameters.csv` at the FX SHA | 19 IDs: gain, duration, pattern enabled, 4× wet, 4× amount, 8 macros. | partial | UI attachment count matches the CSV. Macro tooltip domain is a TL check. | FX. |

#135 HTML (`hardware-reference.html`) already shows separate face/name hints, a bottom mixer, and the words Kit Level. It also uses a different palette (`#070d13`, `#fa8549`, …), paints faders that are not controls, shows eight of sixteen strips, cycles three workspaces rather than four saved layouts, and states that its numbers are dummy. It does not satisfy #127 by itself and must not be embedded.
