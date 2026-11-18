# Component registry — `libs/vox-ui`

SHA `f1d6eb595779c973ae5a139270795aae968804cc`.
`VoxComponents.h` blob `d396cf7d8d5a923dc2e20bb3e430a9b69a3db5a8` includes only:

- `components/VoxPanel.h`
- `components/VoxKnob.h`
- `components/VoxButton.h`
- `components/VoxComboBox.h`
- `components/VoxTabBar.h`
- `components/VoxSectionHeader.h`

Nothing else is a public shared control. Do not describe `VoxFader`, `VoxPad`, `VoxKeyboard`, `VoxMeter`, or `VoxDockWorkspace` as implemented.

## Not a component source

`hardware-reference.html` is not a `libs/vox-ui` module and is not linked into the VST3. It may suggest layout behaviour (one workspace, Grid/List, a bottom mixer). It does not add classes, ParameterIDs, or colours. Docking, Pin, layout persistence, Reset, and a real Select/Audition split remain open #127 requirements (`GAP-DOCK`, `GAP-INSPECTOR`, `GAP-WORKSPACE-PRESETS`, `GAP-RESET-LAYOUT`, `GAP-SELECT-AUDITION`). Proposed classes below stay proposed.


## Existing

| Class | Status | Binding | Paint / a11y / DPI | Must stay shared? |
|---|---|---|---|---|
| `VoxKnob` | existing | Inner `juce::Slider`. Caller builds `SliderAttachment`. `setDoubleClickResetValue` must be given the parameter default. `setStyle(Bipolar)` for pan/tune. `setModulationAmount` is a drawing hint, not a modulation engine. | Diameters from `tokens::size` 36/48/64. Tooltip via `SettableTooltipClient`. No custom `AccessibilityHandler` in the header. | Yes. |
| `VoxButton` | existing | `juce::Button` subclass. Types: Primary, Secondary, Toggle, Danger, Icon. | Own `paintButton`. | Yes. |
| `VoxComboBox` | existing | Thin `juce::ComboBox` wrapper. `.cpp` blob is 25 bytes. | Look comes from `VoxLookAndFeel::drawComboBox`. | Yes. |
| `VoxTabBar` | existing | `onTabChanged(int)`. Not an APVTS attachment. | Keyboard in `keyPressed`. | Yes as a tab strip. Using it as eight primary Drums pages is a product misuse, not a defect of the class. |
| `VoxPanel` | existing | Title string only. | `paint` plus `getContentBounds`. | Yes for simple panels. Dock headers are not this class. |
| `VoxSectionHeader` | existing | Title / section chrome. | Own cpp, 3861 bytes. Not line-audited. | Yes. |
| `VoxLookAndFeel` | existing | Installed by the editor, not by DSP. | Rotary, linear slider, buttons, menus, tooltip, alert. `drawResizableFrame` is empty. Raw hexes listed in [TOKENS.md](TOKENS.md). | Yes. One look. No stock `LookAndFeel_V4` painting in product editors once the Drums UI is replaced. |
| `Tokens.h`, `Typography.h`, `VoxPaint.h` | existing | None. | `VoxPaint::panel` is a flat gradient and 1 px border. That is the current shared paint, not the #135 hardware material. | Yes. |
| `showcase/` | existing | Dev tool. | README: a showcase pass is not editor acceptance. | Keep out of the instrument binary contract. |

`drawLinearSlider` means a JUCE linear slider can be skinned. The Drums mixer does not use one for level. There is still no fader type with a cap, scale, and vertical hit target.

## Proposed — not in the tree

| Proposed type | Responsibility | Not its job |
|---|---|---|
| `VoxFader` | Vertical or horizontal linear value, metal cap, recessed track, double-click default, text value, `SliderAttachment` on the inner slider. | Deciding that the value is Main, Kit, or a send. No audio math. |
| `VoxPad` | Equal hit face, separate select target, mute/solo buttons, binary or later velocity flash, disabled/missing shape. | Kit store, MIDI note allocation, audition. Drums owns `audition(pad, velocity)`. |
| `VoxMeter` | Draw a normalised peak given by the caller. Optional unavailable state. | Reading atomics. No CLIP colour unless the caller passes a documented latch. |
| `VoxDockWorkspace` | Zones, one or two central slots, bottom strip, hide/show, reject illegal drops, serialize layout only. | Parameter IDs, presets, song data. |
| `VoxKeyboard` | MIDI keyboard for products that have one. | Drums #127 does not require it. Absent. Do not add it to Drums to match FX. |

## Product-specific on purpose

Stay in `products/drums` until a second product needs the same behaviour:

- `PadButton`, `WaveformView`, structural DPAD editors, `PatternEditor`, song editor, `MixerPage` strip routing, generator panel, `PatternExport`.
- Processor taps: `takeHitMask`, `takePadMeter`, `takeMainMeter`, `audition`, `activeOutputs`.

Stay in `products/fx`:

- `FxPanel`, `LayerPanel`, `CurveEditor`, family/factory/generate/mutate, `outputPeak`.
- FX includes `components/VoxKnob.h` and uses `vox::ui::VoxKnob`. That is the shared type if the include path is `libs/vox-ui`. The CMake include path was not re-read in this pass (`UNVERIFIED` beyond the namespace).

## Threading

Shared widgets run on the message thread. They must not allocate or paint on the audio thread. Meters and hit masks are consumed on the UI timer (`DrumsPanel::timerCallback` calls `takeHitMask()`; `MixerPage::update` calls `takePadMeter` / `takeMainMeter`). Those functions `exchange` the value to zero. A second reader in the same frame sees empty data. A shared meter must not call them itself.

## Accessibility and DPI

`VoxKnob` exposes the inner slider. `VoxTabBar` handles keys. Dedicated accessibility handlers, Windows 125/150/200% evidence, and 1040×700 layout evidence are **absent** for a docked editor because that editor is absent. `tokens::size::referenceWidth/Height` are 1440×1080. #127 logical minimum remains 1040×700. Those two facts need a TL decision before a dock layout hard-codes 1440.

## Tests to add with the implementation PR, not here

- Attachment: one ParameterID, one control, host automation updates it.
- Select click does not call `audition`. Audition click does.
- `drm.kit.volume` label and DSP comment agree (all outputs).
- Layout restore does not change APVTS.
- `takeHitMask` is not parsed as velocity.
