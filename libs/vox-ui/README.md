# VOX UI

Shared JUCE presentation primitives for Vox Trance Engine.

Active contracts:
- [Design system](../../docs/ui/DESIGN_SYSTEM.md).
- [Instrument workflows](../../docs/ui/WORKFLOWS.md).
- [Product](../../docs/product/PRODUCT_SPEC.md).

## Ownership

`libs/vox-ui/`: target `vox_ui`, namespace `vox::ui`.
`libs/ui/`: target `vst_ui`, product-specific composition.
`apps/vst3/`: shell/editor composition.

Do not create the obsolete `Source/UI/` tree.
Do not link DSP, PluginProcessor, routing or preset implementation into this shared layer.
Product UI composes the shared library; audio processing does not depend on it.
Realtime displays consume safe snapshots.

## Existing implementation

- [Tokens](Tokens.h), [typography](Typography.h) and [look and feel](VoxLookAndFeel.h).
- Panel, knob, button, combo, tabs and section-header primitives under `components/`.
- Component showcase and real-editor capture runner under `showcase/`.

Current baseline: JUCE 9.0.1 and C++20.
Knob diameters 36/48/64 px, 270-degree span, label/always-visible value,
actual-default double-click reset and distinct modulation indication.

Pad banks, instrument rack, piano roll and generator composition belong to product UI,
even though Drums is now part of the same Trance Engine product.
Do not duplicate the shared components per style.

## Build and acceptance

Root CMake links `vst_ui` to `vox_ui`.
`-DVOX_UI_BUILD_SHOWCASE=ON` builds development UI tools; CI enables them.
See [build instructions](../../docs/BUILD.md).
A showcase passing does not establish real-editor visual/functional acceptance.
