# Declarative SOUND layout (#82)

Status: owner-approved order step 3 (#78). Model:
`libs/instrument/SoundLayout.h`. Parser: `vins::parseSoundLayout`. Renderer:
the SOUND page (`MacroPage`, `InstrumentControls::layout`).

## What it is

A `.vins` package can ship, per instrument, a JSON member that arranges that
instrument's real direct parameters into titled sections. The Rack renders it
with its own VOX components, bound through the slot's pool parameters
(`hostparams::poolId(slot, poolIndex)`).

- **No behavior.** The layout holds no values, no code, no scripts and no
  custom graphics.
- **No new state.** It is not part of the instrument state, the parameter spec
  hash or automation identity. Changing a layout never moves automation.
- **Signed with the package.** It is a listed member (`manifest.ui`), so its
  SHA-256 is in the manifest, and a native manifest is signed.

## Schema 1

```json
{
  "voxSoundLayout": 1,
  "sections": [
    {"title": "KICK", "controls": [
      {"parameter": "kick.level", "kind": "knob", "label": "LEVEL"},
      {"parameter": "kit.hat_choke", "kind": "toggle"},
      {"parameter": "seq.mode", "kind": "selector"}
    ]}
  ]
}
```

| Field | Rule |
| --- | --- |
| `voxSoundLayout` | `1`. Other schemas are rejected. |
| `sections` | 1–16 sections, in display order. |
| `title` | 1–32 printable characters. |
| `controls` | At least one per section, in display order. |
| `parameter` | Id of one of the instrument's direct parameters (`poolIndex` 0–127). |
| `kind` | `knob` (floating or integer), `selector` (choice) or `toggle` (boolean). It must match the parameter's type. |
| `label` | Optional, 1–32 printable characters. Default: the parameter's name. |

The member is at most 64 KiB. Unknown keys anywhere are rejected.

## Validation (fail closed)

At package load, after the instrument's descriptor has been imported:
- every control names a real direct parameter of that instrument;
- the kind matches the parameter's type;
- no parameter appears twice;
- every direct parameter appears exactly once.

The page therefore shows exactly the instrument's real controls: no dummy
controls, no legacy APVTS ids and no hidden parameters. A partial panel cannot
pass.

Any violation rejects the package with a diagnostic
(`<package>: ui of <instrument> rejected: …`), so a broken layout never
silently falls back to another page.

## Rendering

- **With a layout.** The SOUND page shows the layout instead of the six
  generic macros: one section title and one control grid per section,
  scrolling vertically.
- **Without a layout.** The SOUND page keeps the macros. The MACROS page keeps
  its generic flat list of all direct parameters for every instrument.
- **Controls.** Each control is a plain JUCE attachment to the pool parameter,
  as on the MACROS page. Host automation, undo and state therefore work the
  same way.
- **Shipped.** Full-on Drums ships `instruments/fullon-drums/sound.json`: ten
  sections holding all 59 parameters.

## Not in schema 1

- Bank tabs.
- Displays and meters (they need typed display sources).
- Custom artwork.

They are left out until a real instrument needs them. The selected VOX Bass
interface (#70, Codex) needs a control matrix agreed with Codex before its
layout is written; a partial Bass panel is not "SOUND COMPLETE".

## Tests

- **`vins_package_tests` / `soundLayouts`.** Covers:
  - schema and shape rejections;
  - unknown, duplicate, missing and wrongly typed parameters;
  - foreign-instrument and unlisted ui entries;
  - attachment to the descriptor;
  - creation through the wrapped provider.
- **`fullon_drums_tests` / `editorSoundLayout`.** Covers:
  - the shipped kit layout (10 sections, 59 controls);
  - every control, visible on the SOUND page, typed and bound to its pool id in layout order;
  - a knob moves its own parameter;
  - the Demo Synth without a layout keeps the macros.
- **Visual Gate.** `drums-sound-layout-1500x920.png` is a CI artifact. It is
  not an acceptance claim; visual acceptance stays with the owner.
