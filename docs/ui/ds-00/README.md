# DS-00 — shared UI inventory and Drums binding matrix

Status: **IN PROGRESS / NO MERGE**. Documentation only. Not a visual sign-off, not a JUCE editor replacement, not a DSP or CI PASS.

`status` in the matrix is **UI coverage only**. `partial` on a host row does not mean the audio engine ignores the parameter. DSP evidence is `dsp_path`. A static read of `processBlock` is not a runtime PASS.

Issue: [#158](https://github.com/ultima-vox/vox-electronic-engine/issues/158).
Functional authority for Drums remains [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127).
Visual reference only: [#135](https://github.com/ultima-vox/vox-electronic-engine/pull/135).

## SHAs read for this inventory

| Tree | SHA | What it is |
|---|---|---|
| Docs branch base `next/standalone-suite` | `f1d6eb595779c973ae5a139270795aae968804cc` | Where this branch starts. `libs/vox-ui` blob for `VoxComponents.h` is `d396cf7d8d5a923dc2e20bb3e430a9b69a3db5a8`. `Tokens.h` blob is `c1c9a2dabc87f4db6487868a7ab04c2f13f1d6c3`. |
| Drums integration `integration/vox-drums-1.0` | `ef13daa3887dbd6079054384b04be7b65849d9ef` | Binding source for Drums. Same `VoxComponents.h` and `Tokens.h` blobs as the base. **Not** the same `products/drums` tree as the base. |
| Stale Drums copy on the base | `f1d6eb595779c973ae5a139270795aae968804cc` | `products/drums/parameters.csv` blob `4f5a09e103286f34a99b719be8e88c3a106ba6d8` (6249 bytes). Integration CSV blob `3b8e85dc031db76cdeeb304e7fbfb89ce8204b46` (8880 bytes, 174 rows). `DrumsEditor.cpp` blobs differ (`e2c70905` vs `da5f17f7`). Do not implement the dock UI against the base copy. |
| FX draft [#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156) | `1587dbb4a01f7b01c7a8751562bbe6c57fe6fe82` on `arena/d9776bd9-vox-electronic-engine` | Re-read for this correction. Earlier matrix text used `a43c46e4`. Issue #158 originally cited `685241ad`. Neither older SHA is this HEAD. FX code was not edited. |
| Hardware HTML reference branch `design/vox-drums-hardware-reference` | `74513b1ce4e670d8126ccd78a8ce1b02a4764789` | `docs/ultima-vox-music/drums/ux-v3/hardware-reference.html` blob `9e483a682c706eca5edf933a8686e422d63a5027`. Visual reference only. |

## Files

| File | Role |
|---|---|
| [COMPARATIVE_TABLE.md](COMPARATIVE_TABLE.md) | #127 / #135 / shared components versus code |
| [COMPONENT_REGISTRY.md](COMPONENT_REGISTRY.md) | What `libs/vox-ui` actually exports, and proposed gaps |
| [TOKENS.md](TOKENS.md) | Token values, provenance, extra literals |
| [UI_BINDING_MATRIX.csv](UI_BINDING_MATRIX.csv) | 263 rows. 193 host IDs (174 `drm.*` + 19 `fx.*`) plus structural, layout, telemetry, label gap, macro UI note |
| [STATE_BOUNDARIES.md](STATE_BOUNDARIES.md) | APVTS, kit store, and layout are three states. Undo and relaxed-atomic limits |
| [HANDOFF.md](HANDOFF.md) | Order of later implementation. Not permission to start it |
| [HTML_PROTOTYPE_VS_127.md](HTML_PROTOTYPE_VS_127.md) | Interactive HTML versus #127 and #135. Neon palette is not imported. |

## Non-goals

- No C++ on this branch.
- No pictures.
- No edits to `main`, `integration/vox-drums-1.0`, the FX arena branch, `#152`, or `#153`.
- No claim that a showcase, HTML reference, or this matrix is audio quality or a DAW PASS.
- Cubase/REAPER stay at the final integrated product gate.

## Vocabulary

`status` is UI coverage, nothing else.

`existing` — the cited class or rejection is in the cited file.
`partial` — a real control or binding exists, but not the #127 surface. For a host ID this still allows `dsp_path=confirmed-static`.
`absent` — the required UI behaviour was not found. For `STRUCT-output` and `STRUCT-voiceLimit` it means there is no host parameter, not that the member is unread.
`proposed` — a later shared component, not present in `VoxComponents.h`.

`dsp_path`:

- `confirmed-static` — a cited function reads the value. Not a process run and not a PASS.
- `store-pull` — kit store, then `store_.pull`. Not host automation.
- `telemetry` — meter or hit mask. Not a ParameterID.
- `not-host` — layout, command, or navigation.
- `n/a` — the row is a label, a shared class, or not a value.

`runtime_rt` stays `unverified` unless a process was actually run. DS-00 did not run audio.

`kind=ui-label-gap` is not a host ID. `kind=modulation-ui` is not an extra `fx.macro` ID.

## Host ID check

Counted from this CSV after the correction:

- `kind=host-param`: 193 rows, 193 unique `parameter_or_api`.
- 174 `drm.*` equal `products/drums/parameters.csv` at `ef13daa3`.
- 19 `fx.*` equal `products/fx/parameters.csv` at `1587dbb4`.
- `GAP-KIT-LABEL` is `ui-label-gap`. Its API cell is the canonical `drm.kit.volume`, and it is not in the host set.
- `FX-MACROS-UI` is `modulation-ui` and has an empty API cell.

`domain` / `range_code` are the code ranges (`0.01..10`, `0..0.85`), not the `setprecision(9)` print. The old print is `csv_printed_domain`. `default_plain` is the float32 value without that print noise. `default_csv` keeps the file text.


## TL rulings — 2026-11-18

These rulings are recorded. They do not close the open requirements below.

- `hardware-reference.html` (`74513b1`) is an interactive reference for individual layout and behaviour ideas only. [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127) remains the functional authority. The HTML does not replace it.
- Visual direction is **not approved**. The rejected neon sheet is not imported.
- Current `Tokens.h` values are an inventory, not a signed-off palette.
- HTML colours `#070d13`, `#fa8549`, and `#3ce4d4` are not approved and are not written into `Tokens.h`.
- Docking, Inspector Pin, layout persistence, Reset Workspace, and separate Select / Audition stay **open requirements**. Rows: `GAP-DOCK`, `GAP-INSPECTOR`, `GAP-WORKSPACE-PRESETS`, `GAP-RESET-LAYOUT`, `GAP-SELECT-AUDITION`.
- The eight primary pages in `DrumsPanel::pageName` are the current editor. They are not the new architecture and must not be carried forward as primary navigation.
- The HTML contains demonstration elements that are not bound to DSP: fixed `145.0 BPM`, named dummy kits, CSS fader caps, and a Main strip that always prints `OVER`.
