# Design tokens

Provenance: `libs/vox-ui/Tokens.h` blob `c1c9a2dabc87f4db6487868a7ab04c2f13f1d6c3` at both `f1d6eb595779c973ae5a139270795aae968804cc` and `ef13daa3887dbd6079054384b04be7b65849d9ef`.

These are the **current shared constants**. They are not an owner sign-off of the #135 hardware palette. #135 HTML uses other literals (`--bg:#070d13`, `--orange:#fa8549`, `--aqua:#3ce4d4`, and more). Do not merge those into `Tokens.h` from this document.

Parser: every colour below is `juce::Colour::fromString` with an AARRGGBB literal. That is the only `fromString` call site confirmed in this pass (`Tokens.h`). `VoxLookAndFeel.cpp` does **not** call `fromString`. `DrumsEditor.cpp`, `DrumsPages.cpp`, and `FxEditor.cpp` at the cited SHAs do not contain `fromString`. A full-tree search was not done. Other call sites are `UNVERIFIED`.

## Colour

| Name | Stored literal | RGB | Scope |
|---|---|---|---|
| `colour::background` | `FF06121D` | `#06121D` | window fill |
| `colour::panel` | `FF0B1925` | `#0B1925` | panel |
| `colour::panelRaised` | `FF102230` | `#102230` | raised panel |
| `colour::control` | `FF0A1722` | `#0A1722` | control well |
| `colour::graph` | `FF051019` | `#051019` | graph |
| `colour::border` | `FF1D3B50` | `#1D3B50` | border |
| `colour::borderSubtle` | `FF122C3D` | `#122C3D` | subtle border |
| `colour::accent` | `FF00DDF5` | `#00DDF5` | shared accent |
| `colour::accentHover` | `FF42EEFF` | `#42EEFF` | hover |
| `colour::accentDim` | `FF087B92` | `#087B92` | dim accent |
| `colour::text` | `FFE9F3FA` | `#E9F3FA` | primary text |
| `colour::textSecondary` | `FF9AB2C5` | `#9AB2C5` | secondary text |
| `colour::textMuted` | `FF587487` | `#587487` | muted text |
| `colour::danger` | `FFE4425D` | `#E4425D` | danger |
| `colour::success` | `FF32D296` | `#32D296` | success |
| `colour::warning` | `FFE3B341` | `#E3B341` | warning |

`accent`, `accentHover`, `accentDim`, `control`, `borderSubtle`, `textMuted`, `success` are present in code. This document does not call them approved hardware tokens. #127 allows cyan for focus/activity and a Drums copper for the primary musical action. A copper constant is **not** in `Tokens.h`. Drums uses `processor.product().accent`. The numeric accent was not read in this pass (`UNVERIFIED`).

## Spacing, radius, size, type

| Name | Value | Type |
|---|---|---|
| `spacing::xs` … `xxxl` | 4, 8, 12, 16, 20, 24, 32 | `int` px |
| `radius::small/medium/large` | 4, 6, 8 | `float` |
| `size::controlHeight` | 32 | `int` |
| `size::tabHeight` | 36 | `int` |
| `size::knobSmall/Normal/Large` | 36, 48, 64 | `int` |
| `size::instrumentSlotMinHeight/MaxHeight` | 54, 58 | `int` |
| `size::referenceWidth/Height` | 1440, 1080 | `int` |
| `typography` title / section / label / value | 26 / 14 / 12 / 11 px | `float` |

`instrumentSlotMin/MaxHeight` and 1440×1080 are rack-era numbers in a file that also serves standalone instruments. They are not the #127 minimum (1040×700).

## Literals in `VoxLookAndFeel.cpp` that are not tokens

Confirmed by scan of that file at blob `4ddb0cafd37f2359023f6dc6aab02dac79584182`:

- `0xff050c13`
- `0xff0a1520`
- `0xff122433`
- `0xff2b4152`

196 `Colour` mentions in the same file. They are not a second approved palette. A later paint PR should either move them into named tokens or delete them. This DS-00 does not do that.

## Material

#135 asks for matte metal, rubber pads, recessed fader tracks, restrained arcs. `VoxPaint::panel` is a small vertical gradient and a hairline. That gap is real. It is not a licence to copy another product, and it is not closed by the HTML file.
