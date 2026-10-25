# VOX UI

Shared JUCE UI foundation for the Ultima Vox plugin family.

Canonical documents:

- [`docs/UI_DESIGN_SYSTEM.md`](../../docs/UI_DESIGN_SYSTEM.md)
- [`docs/UI_COMPONENT_CATALOG.md`](../../docs/UI_COMPONENT_CATALOG.md)
- [`docs/UI_VISUAL_REFERENCE_SPEC.md`](../../docs/UI_VISUAL_REFERENCE_SPEC.md)
- [`docs/VOX_UI_FAMILY_ARCHITECTURE.md`](../../docs/VOX_UI_FAMILY_ARCHITECTURE.md)
- [`docs/UI_FOUNDATION_CHECKLIST.md`](../../docs/UI_FOUNDATION_CHECKLIST.md)
- [`docs/UI_FOUNDATION_DECISIONS.md`](../../docs/UI_FOUNDATION_DECISIONS.md)

## Canonical location and ownership

This directory is the only shared VOX design-system implementation in this repository:

```text
libs/vox-ui/   target: vox_ui   namespace: vox::ui
libs/ui/       target: vst_ui   Electronic Engine product UI
apps/*         thin plugin/application shells
```

`Source/UI/` is not a valid path in this repository and must not be created.

## Scope

`vox-ui` contains only reusable family-level presentation primitives:

- design tokens;
- typography factories and canonical text sizes;
- `VoxLookAndFeel`;
- knobs, buttons, combo boxes and tab primitives;
- panels and section headers;
- generic graph/meter visual language;
- shared interaction and visual-state rules.

Product-specific widgets stay outside this module. In this repository they belong under `libs/ui/` (`vst_ui`). Examples:

- instrument rack and Part header;
- Bass / Acid / Lead / Atmos product panels;
- piano roll;
- step sequencer;
- arpeggiator / phrase editor;
- pattern-generator UI;
- modulation-matrix composition;
- Electronic Engine page composition.

Other products compose the same shared layer rather than copying it:

- Vox Drums Engine: drum pads, sample editor, drum sequencer;
- Mastering Engine: spectrum, loudness/true-peak/GR meters, module chain.

A product widget may move into `vox-ui` only after it has a second real consumer and can be made product-independent without leaking DSP or product state into the shared layer.

## Dependency rule

Allowed:

```text
product UI (vst_ui) -> vox_ui -> JUCE GUI
apps/* -> product UI
```

Forbidden:

```text
vox_ui -> product DSP
vox_ui -> PluginProcessor
vox_ui -> rack / preset / transport / instrument engine
DSP realtime path -> vox_ui
```

The shared module may depend on JUCE GUI/graphics modules only. UI visualisations must consume safe snapshots/atomics/message-thread data, never mutable realtime DSP objects directly.

## JUCE baseline

The repository currently pins:

```text
JUCE 9.0.1
C++20
```

## UI-1 implementation candidate

The UI-1 branch now contains:

```text
Tokens.h
Typography.h
VoxLookAndFeel.h/.cpp
VoxComponents.h        umbrella
components/
  VoxPanel.h/.cpp
  VoxKnob.h/.cpp
  VoxButton.h/.cpp
  VoxComboBox.h/.cpp
  VoxTabBar.h/.cpp
  VoxSectionHeader.h/.cpp
showcase/
  CMakeLists.txt
  Main.cpp
```

`VoxLookAndFeel` implements the shared fallback grammar for button background, rotary slider, combo box, toggle, linear slider and popup-menu items.

`VoxKnob` owns specialised knob rendering and semantic state. Its internal slider is used only for interaction/value/attachment. The canonical layout is label top / knob centre / always-visible value bottom. Small/Normal/Large diameters are 36/48/64 px, the rotary span is 270 degrees, and modulation appears automatically when a modulation snapshot is non-zero.

Double-click reset is not hard-coded to zero: callers configure the real parameter default through `setDoubleClickResetValue()`.

The implementation is not considered accepted until the Windows build, full test suite and real showcase screenshots have passed the UI-1 acceptance gate.

## CMake

This directory provides the reusable target:

```cmake
add_subdirectory(libs/vox-ui)
target_link_libraries(vst_ui PUBLIC vox_ui)
```

The showcase is dev-only and disabled by default:

```cmake
-DVOX_UI_BUILD_SHOWCASE=ON
```

CI explicitly enables it so shared UI sources compile on every UI PR without making the executable part of normal library consumers.

## Namespace

Reusable code lives under:

```cpp
namespace vox::ui
```

Product-specific UI should use its own namespace and compose `vox::ui` components rather than modifying or forking them locally.

## Change policy

A change to shared colours, spacing, typography, component states or common control behaviour is a design-system change and must be reviewed as such.

Do not fork a shared component inside one plugin simply to obtain a different local style. Extend the shared component when the behaviour is genuinely family-wide; otherwise compose it from the product layer.
