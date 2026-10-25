# Trance Engine UI design system

This is the active UI engineering contract. The old multi-product canon is
[historical](../../archive/electronic-engine/docs/UI_DESIGN_SYSTEM.md).
The new instrument layout still requires owner visual acceptance.

## Reuse the implementation

- Shared tokens: [Tokens.h](../../libs/vox-ui/Tokens.h).
- Typography: [Typography.h](../../libs/vox-ui/Typography.h).
- Look and feel/components: [VOX UI](../../libs/vox-ui/README.md).
- Product composition: `libs/ui/` and `apps/vst3/`.
- Do not create the historical `Source/UI/` tree.

Shared presentation depends on JUCE GUI only.
Product UI composes it; DSP/rack/provider state does not depend on UI.
Read realtime displays from safe snapshots, never mutable audio objects.

## Existing tokens

| Purpose | Values |
| --- | --- |
| Window / panel / raised panel | #06121D / #0B1925 / #102230 |
| Control / graph | #0A1722 / #051019 |
| Accent / hover / dim | #00DDF5 / #42EEFF / #087B92 |
| Text / secondary / muted | #E9F3FA / #9AB2C5 / #587487 |
| Danger / success / warning | #E4425D / #32D296 / #E3B341 |
| Spacing | 4 / 8 / 12 / 16 / 20 / 24 / 32 px |
| Radii | 4 / 6 / 8 px |
| Knob diameters | 36 / 48 / 64 px |
| Control / tab height | 32 / 36 px |
| Typography | Title 26, section 14, label 12, value 11 px |

These values reflect the current token files; this cleanup changes no tokens.
Old token reference canvas is 1440x1080, not a new fixed-window requirement.

## Interaction

- One knob grammar: label, 270-degree arc, always-visible value, distinct modulation layer.
- Double-click resets to the actual parameter default; Shift-drag adjusts finely.
- Normal/hover/pressed/focused/active/disabled states; critical state is not colour-only.
- Display numeric values/units; allow keyboard focus and readable disabled explanations.
- Stable automation attachments with proper gestures; no independent UI copy of DSP truth.
- Graphs show real envelopes, filter/oscillator state or an explicitly labelled preview.
- No decorative fake meters, generic JUCE default appearance or instrument-specific component forks.
- Drums uses the same controls and may add pad/lane composition in the product layer.

## Layout and acceptance

Functional controls and musical editors take precedence over artwork.
Use relative layout/nested areas, consistent gutters, explicit minimums and scaling.
Proposed checks: 1180x760 and 1500x920 logical sizes; Windows scaling 100/125/150/200%.
Final minimum and layout are determined by real editor checks.
New artwork/visual mockups require the owner's explicit request to draw.

Visual Gate A: actual editor screenshots/layout review and explicit owner acceptance.
Functional Gate B: every shipped control binds to real state/DSP, with automation,
preset restore and host checks. Compilation/showcase smoke tests do not replace either gate.
