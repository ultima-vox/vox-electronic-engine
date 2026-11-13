# DUIS v2 — VOX Drums layout and UI-state contract (Architecture Guard proposal)

Status: **revision 2, after Architecture Guard review 6864584060** on PR #130. Approved there:
Q1, Q2, Q3, Q5, Q6. Q4 was rejected and is withdrawn (§10). This revision resolves P1 (no
rewrite of future data), P2 codec order, P2 budget and P2 restore threading. Still a
proposal for Architecture Guard review. Documentation only: no code, no JUCE UI,
no change to `drm/production-candidate` (PR #125) or to any other branch. It is required
before Gate 2 (native UI v3, issue #127). Task order: issue #127 comment 6863921664.

Baseline verified: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2`,
JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`.

"DUIS v2" here means the second generation of the Drums UI state. §2 explains why it is
**not** a new version number of the existing `DUIS` section.

## 1. Facts at `c7c29bb` that constrain the design

| # | Fact | Source |
|---|---|---|
| F1 | State = one container: product tag, `u32 schema` (1), sections, CRC. A schema above the build's maximum is rejected. | `libs/vox-preset/StateContainer.{h,cpp}`, `SuiteProcessor.h` |
| F2 | Restore is transactional: decode and validate everything, then commit. Any rejection leaves the live instrument unchanged. | `SuiteProcessor::setStateInformation` |
| F3 | `UIST` (suite level) = `u32 width, u32 height`. A size outside `EditorLimits` 1040–1500 × 700–920 **rejects the whole restore** ("UI size out of range"). | `SuiteProcessor.h` |
| F4 | `DUIS` v1 = `u32 version (1)`, then `u8 page, pad, songBar, patternBar, exportScope`. Exactly 9 bytes; any other version or size, or a value out of range, **rejects the whole restore**. Missing `DUIS` gives defaults. | `Processor.cpp` `decodeUiState`; DRM-05-07 doc §4.4 |
| F5 | Sections of an unknown writer are kept **byte for byte** and written back by every save. They are accepted only while they fit next to `productStateReserve()` (736 KiB) in the 1 MiB container. | `SuiteProcessor.h` |
| F6 | The editor writes UI state into the processor on every change (`setUiState`). The processor saves it, so saving does not depend on whether the editor is open. | `DrumsEditor.cpp`, `DrumsPages.cpp`, `Processor.cpp` |
| F7 | Kit/pad/pattern/song presets contain no UI state. | `Presets.cpp` |
| F8 | Per-user data lives under `userApplicationDataDirectory/ULTIMA VOX/VOX Drums/` (Presets, Samples), with an environment override for tests. | `Presets.cpp`, `SampleLibrary.cpp` |
| F9 | JUCE `updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true))` becomes VST3 `IComponentHandler2::setDirty (true)`. | JUCE `juce_audio_plugin_client_VST3.cpp` |

Consequences for compatibility with older builds:

- Raising the `DUIS` version (F4) would make the shipped build `c7c29bb` refuse every newer
  project entirely, audio state included. So would raising the container schema (F1).
- Saving an editor larger than 1500 × 920 into `UIST` (F3) would do the same. UI v3 needs
  1600 × 1000 evidence.

## 2. Decision proposed

**Keep `DUIS` v1 and `UIST` byte-compatible, and add one new product section `DLAY` v1 for the
layout.**

- `c7c29bb` sees `DLAY` as an unknown section (F5). It keeps the section byte for byte and writes
  it back, so a project survives a round trip through the old build with nothing lost.
- `UIST` is always written within the old limits (§5). The real editor size goes in `DLAY`.
- `DUIS` keeps its fields and meaning. `page` and `exportScope` are preserved for old builds
  (§8).
- No container schema change. No host parameter, DSP, bus, pattern, song, kit or preset change.

## 3. What is stored where

| Class | Where | Contents | Travels with the project |
|---|---|---|---|
| **Project UI state** | plugin state: `UIST`, `DUIS` v1, `DLAY` v1 | editor size; selected pad, song bar, pattern bar; the layout (zones, panels, tabs, splits, Mixer state and heights, Pad Rack view, parameter-lane choices, Inspector pin) | yes |
| **Per-user preferences** | `…/ULTIMA VOX/VOX Drums/Workspaces/workspaces.json` | named user workspaces (each one a `DLAY` payload) | no |
| **Ephemeral** | memory only | hover, focus ring, drag in progress, dock-target preview, menus, tooltips, scroll and zoom positions, meter values, peak holds, OVER latches, hit flashes, degradation forced by a small window (§5) | no |

Factory workspace presets (Beatmaking, Composition, Sound Design, Mixing) are code constants, not
files. "Reset Workspace" restores the active preset. Workspace presets are separate from
kit/pad/pattern/song presets (F7).

## 4. `DLAY` v1 encoding

All values are little-endian, the same as the other sections.

```
u32 version = 1
u16 recordCount            (≤ 32)
record × recordCount:
  u8  type
  u16 length               (payload bytes, ≤ 256)
  u8  payload[length]
```

The whole section is at most 2048 bytes.

**Ordering rule (one rule, for known and unknown types alike).** Record types are **strictly
ascending**: each type appears at most once, and an unknown type sits at its numeric position
among the known ones. A stream that breaks the rule is **non-canonical and invalid** (§7). It is
never silently re-sorted.

**Preservation.** Two levels are kept apart:

- *Per record:* an unknown record's payload is kept byte for byte and re-emitted at its type
  position.
- *Whole section:* the original section bytes are written back unchanged until a committed
  layout edit (§7).

**Panel IDs** (closed set for v1). Hidden panels are the ones that appear in no zone record.

| ID | Panel | Allowed zone |
|---|---|---|
| 1 | Pad Rack | left, right |
| 2 | Library | left, right |
| 3 | Inspector | left, right |
| 4 | Generator | left, right |
| 5 | Routing | left, right |
| 6 | Settings | left, right |
| 16 | Step Sequencer | central |
| 17 | Song Arranger | central |
| 18 | Sound Editor | central |
| 19 | Sample Editor | central |
| — | Mixer | bottom only (implicit; no ID needed) |

**Records**

| Type | Name | Payload | Valid values |
|---|---|---|---|
| 1 | WORKSPACE | `u8 basePreset` (0 Beatmaking, 1 Composition, 2 Sound Design, 3 Mixing), `u8 modified` | 0–3; 0/1 |
| 2 | EDITOR_SIZE | `u16 width, u16 height` (logical px) | 1040–2560 × 700–1600 |
| 3 | ZONE_LEFT | `u16 width`, `u8 rail`, `u8 groupCount`, then per group: `u16 heightPermille, u8 activeIndex, u8 tabCount, u8 panel[tabCount]` | width 196–640; rail 0/1; 1–3 groups; permille sum 1000 ± 3; tabs 1–6 |
| 4 | ZONE_RIGHT | same as 3 | same as 3 |
| 5 | CENTRAL | `u8 editorCount`, `u8 split` (0 above/below, 1 side by side), `u16 ratioPermille`, `u8 panel[editorCount]`, `u8 maximized` (255 = none, else 0/1) | count 1–2; ratio 150–850 |
| 6 | MIXER | `u8 state` (0 collapsed, 1 compact, 2 expanded), `u16 compactHeight, u16 expandedHeight`, `u8 bank` (0 = 1–8, 1 = 9–16) | heights 120–220 / 280–560 |
| 7 | PAD_RACK | `u8 view` (0 grid, 1 list) | 0/1 |
| 8 | PARAM_LANES | `u8 count`, `u8 kind[count]` (0 velocity, 1 chance, 2 ratchet, 3 microtiming) | count 0–2 |
| 9 | INSPECTOR_PIN | `u8 kind` (0 none, 1 pad, 2 steps, 3 song bar), then for pad `u8 pad`; for steps `u8 slot, u8 variation, u8 bar, u8 lane, u8 firstStep, u8 lastStep`; for song bar `u8 bar` | ranges as in DUIS/DSEQ |
| 10 | SOUND_SECTIONS | `u8 collapsedMask` (bit per Sound Editor section) | any |

**Validation.** A known record is valid only if all of the following hold:

- its length matches its layout;
- every value is in range;
- every panel appears at most once across all zone records;
- each panel sits in an allowed zone (table above).

If any check fails, the **whole** `DLAY` is invalid; §7 says what happens then.

**Floating windows: none.** Spec v3 forbids floating OS windows, so v1 has no window-bounds
record. If a later version adds them, that is a new record type, which a v1 reader keeps and
ignores.

**Canonical form.** `encode (decode (b)) == b` holds for every **valid** v1 stream.

- A valid stream has strictly ascending types and correct lengths.
- Each known payload is in its single encoding: fixed widths, no trailing bytes, and the
  variable parts sized exactly by their counts.
- Unknown payloads are kept as they are, so they round trip by construction.

A known payload with extra or missing bytes is invalid; it is not normalised.

## 5. Editor size, display and DPI

- All sizes are **logical pixels**. The host's content scale (125/150/200 %) is applied by JUCE
  and VST3 at run time and is **not stored**. A layout therefore looks the same at every DPI.
- **Saving:** `DLAY` EDITOR_SIZE holds the real size (up to 2560 × 1600). `UIST` holds the same
  size clamped to 1040–1500 × 700–920, so `c7c29bb` still accepts the project (F3).
- **Restoring:** the size comes from `DLAY` EDITOR_SIZE if it is valid, otherwise from `UIST`.
- **Opening the editor:**
  - If the saved size does not fit the work area of the display the host places the editor on,
    the editor opens clamped to fit, for this session only.
  - The saved size changes only when the user resizes. A project moved to a smaller screen and
    back keeps its size.
- **Degradation (spec v3 §1 / schematic):** side zones to rails, second editor to a tab, Mixer
  to Collapsed, Pad Rack Grid to List, fewer parameter lanes.
  - All of it is **computed from the current size every time and never stored**. `DLAY` holds
    the user's intended layout.
  - Growing the window restores the intended layout exactly.
  - Determinism: the same `DLAY` bytes and the same editor size always give the same visible
    layout.

## 6. Lifecycle: one owner, editor open or closed

- **Owner.** The processor holds the layout: the decoded struct, plus the raw bytes of any
  preserved record types or of an unsupported `DLAY` version. It sits under the existing
  `settingsLock_`, on the message and host threads only. The audio thread never reads or writes
  UI state.
- **Editor.** On construction it reads the layout and selection from the processor. It writes a
  change back when the gesture is **committed**:
  - a drop at the end of a drag;
  - divider release;
  - tab change;
  - Mixer state or height change;
  - view switch;
  - pin or unpin;
  - workspace preset applied or reset.

  Drags in progress are never written.
- **Editor closed.** A save writes the processor's copy. A restore replaces it. The next editor
  open shows it. Nothing is lost because the editor is closed (F6).
- **Restore while the editor is open: threads and handoff.**
  - `setStateInformation` can be called by the host on any non-audio thread.
  - It decodes, validates and commits the layout under the processor's state lock. It never
    touches a `Component`.
  - Then it increments an atomic `layoutGeneration` and calls `sendChangeMessage()` on a
    processor-owned `juce::ChangeBroadcaster`. That call only posts a message, so it is safe from
    any thread.
  - On the message thread, each registered editor compares `layoutGeneration` with the value it
    last applied. If it differs, the editor copies the layout under the lock, applies it,
    cancels a drag in progress and closes menus.
  - The editor registers in its constructor and **unregisters in its destructor**. Both run on
    the message thread, which is also where the broadcaster delivers. An editor closed while a
    notification is pending is therefore never called. A notification arriving after the close
    finds no listener.
  - No UI is changed from a host or audio callback. The audio thread never touches layout state.
- **Save during a drag.** The last committed layout is saved.
- **Undo.** Layout changes are not project-undo steps and never touch audio state.
- **Dirty flag.**
  - After a committed layout change the processor calls `updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true))`. JUCE turns this into VST3 `setDirty (true)` (F9).
  - Whether Cubase and REAPER then mark the project modified is **not known** and must be tested
    (T19).
- **Host opening several editors.** All editors read and write the same processor copy. The last
  committed change wins, and the others are refreshed by the same notification.

## 7. Restore rules and fallbacks

| Project contains | Result |
|---|---|
| no `DUIS`, no `DLAY` (state from before DRM-05) | Beatmaking preset; selection defaults (as today) |
| `DUIS` v1, no `DLAY` (written by `c7c29bb`) | layout from the migration table (§8); selection from `DUIS` |
| `DUIS` v1 + valid `DLAY` v1 | layout from `DLAY`; selection from `DUIS` |
| `DLAY` with unknown record types | known records used; unknown records kept and written back unchanged |
| `DLAY` version > 1 (newer writer) | Layout for this session from the `DUIS` migration. The future `DLAY` bytes are **never rewritten**: every save writes them back unchanged, and layout edits in this session apply on screen only. The Workspace menu shows "Layout saved by a newer VOX Drums — changes here are not saved" plus **Reset layout…**. Only Reset, after a confirmation that names the loss ("Replace the newer layout with this version's layout? The newer layout will be lost."), writes a v1 `DLAY`. |
| `DLAY` v1 invalid (truncated, wrong length, out of range, duplicate or misplaced panel, non-ascending types, > 2048 bytes) | Layout from the `DUIS` migration. The project, audio state included, **is restored**. "Layout data was invalid; a default layout is shown" appears in Settings › Diagnostics. The invalid bytes are **kept** and written back unchanged while the user only views. The **first committed layout edit, or Reset**, replaces them with a valid v1 `DLAY`. They are the plugin's own corrupt v1 data, not a newer feature, so this replacement loses nothing usable. |
| Inspector pin pointing to an object that no longer exists | Inspector shows "unavailable" with Unpin / Pin current selection (spec v3 §2); nothing is edited |
| `DUIS` v1 malformed | unchanged from today: the whole restore is rejected (F4) |
| `UIST` out of the old range | unchanged from today: rejected (F3). The new build never writes such a `UIST` (§5) |

**Round trip through an old build.** `c7c29bb` keeps `DLAY` unchanged but may change
`DUIS.page`. When the new build reopens the project, it uses `DLAY`, so a page change made in
the old build is not reflected in the layout. Selections (pad, song bar, pattern bar) still come
from `DUIS`.

**Requested exception.** `DLAY` is UI-only. An invalid `DLAY` does not reject the project; the
other product sections stay strict. This deliberately departs from DRM-05-07 §4.4 ("malformed
section rejects the whole state"), for this one UI-only section, and needs Architecture Guard
approval (§12 Q2).

## 8. Migration from `DUIS` v1

| `DUIS.page` (v1) | Workspace | Notes |
|---|---|---|
| 0 PERFORMANCE | Beatmaking | Pad Rack Grid |
| 1 SOUND | Sound Design | Sound Editor for `DUIS.pad` |
| 2 PATTERN/SONG | Composition if Song mode is on (`DSNG`), else Beatmaking | Step Sequencer shows `DUIS.patternBar`; Song Arranger selects `DUIS.songBar` |
| 3 MIXER | Mixing | Mixer Expanded |
| 4 GENERATE | Beatmaking | Generator tab active in the right zone |
| 5 ROUTING | Mixing | Routing tab active |
| 6 PRESETS | Beatmaking | Library active in the left zone |
| 7 ADVANCED | Beatmaking | Settings tab active in the right zone |

- `DUIS.pad`, `songBar` and `patternBar` keep their meaning and are read and written as today.
- `DUIS.exportScope` is not used by UI v3: the drag scope is fixed by the handle. It is written
  back **unchanged**, so an old build keeps its setting.
- `DUIS.page` is written back **unchanged** unless the user changes the workspace in this
  session. Then it is written as the nearest old page: Beatmaking 2, Composition 2, Sound
  Design 1, Mixing 3. An old build opening the project then lands on a sensible page.

## 9. Per-user workspaces file

- **Location:** `userApplicationDataDirectory/ULTIMA VOX/VOX Drums/Workspaces/workspaces.json`.
  Tests override it with the environment variable `VOX_DRUMS_WORKSPACES`, the same pattern as
  Presets and Samples (F8).
- **Content:**

  ```
  { "version": 1, "workspaces": [ { "name": "…", "layout": "<base64 DLAY payload>" } ] }
  ```

  At most 32 entries; names are 1–64 characters and unique (case-insensitive).
- **Access:**
  - Read when the Workspace menu opens. Written on Save Workspace As… / Delete.
  - Message thread only. The write goes to a temporary file and is then renamed.
  - A corrupt file is ignored, renamed `workspaces.json.bad`, and the menu shows factory presets
    only.
- **Applying a user workspace** copies its layout into the project `DLAY` (WORKSPACE.modified
  = 1). The project does not reference the file. A project opened on another machine keeps its
  own layout; only the named list is missing there.

## 10. Size budget (Q4 withdrawn)

- `DLAY` is at most 2048 bytes plus a 16-byte section header: 2064 bytes.
- **No change to `productStateReserve()` (736 KiB = 753,664 bytes).** Its own comment at
  `c7c29bb` sizes the product's largest state at about 705 KB. Even counted as 705 KiB
  (721,920 bytes), that leaves about 31 KB, enough for `DLAY` (2064 bytes) and the proposed
  `DROL` (37 bytes, PR #132).
- The room for unknown-writer sections is unchanged (F5).
- **Boundary fixture (T21):** the largest product state the plugin can produce, plus a
  2048-byte `DLAY`, plus `DROL`, encoded and measured, must fit `productStateReserve()`. If it
  does not, the reserve change comes back for review with the measured number.

## 11. Acceptance tests (Gate 2, before the UI ships)

- **Codec:**
  - **T1** `DLAY` v1 round trip: random valid layouts, `encode (decode (b)) == b`.
  - **T2** Every record: each out-of-range value, wrong length, duplicate type, duplicate panel
    and misplaced panel is rejected.
  - **T3** Size limits: 2048 bytes accepted, 2049 rejected; `recordCount` 33 rejected.
- **Unknown data and fallbacks:**
  - **T4** Unknown record types interleaved between known types (e.g. 1, 3, 11, 12, 20) are kept
    at their positions and written back byte for byte. Out-of-order or duplicate types make the
    stream invalid.
  - **T4b** A known payload with a trailing extra byte is invalid; it is not normalised.
  - **T5** Synthetic future data (`DLAY` v2): load, make several layout edits, save, reopen. The
    saved `DLAY` is byte-identical to the original and the notice is shown. Only Reset with
    confirmation writes v1, and Cancel leaves it unchanged.
  - **T6** An invalid `DLAY` restores the project with all audio sections bit-identical to the
    fixture. The layout equals the `DUIS` migration and the diagnostics text is present.
    Save without edits writes the invalid bytes back unchanged. After one layout edit, the save
    writes a valid v1.
- **Migration:**
  - **T7** `DUIS` pages 0–7, with Song mode on and off: expected workspace, active tabs and
    selection (§8 table).
  - **T8** A pre-DRM-05 state with no `DUIS` and no `DLAY` gives Beatmaking.
  - **T9** `exportScope` and an unchanged `page` are written back byte-identical.
- **Old-build compatibility:**
  - **T10** A state written by the new build, decoded with the `c7c29bb` rules: accepted, `DLAY`
    kept as unknown, `UIST` valid. Implemented as a test running the old decode functions on a
    committed fixture, plus one manual load of the `c7c29bb` artifact.
  - **T11** A 1600 × 1000 editor saves `UIST` = 1500 × 920 and `DLAY` = 1600 × 1000, and restores
    at 1600 × 1000.
- **Lifecycle and determinism:**
  - **T12** Editor closed: change the layout, close, save, restore into a new instance, open:
    identical layout.
  - **T13** Restore while the editor is open (with a drag in progress): the layout is replaced,
    the drag is cancelled, no crash.
  - **T13b** A restore on a non-message thread, while the editor is being closed on the message
    thread, run many times (TSan build): no callback into a deleted editor, no data race; the
    next editor opens with the restored layout.
  - **T14** Determinism: the same bytes at 1040×700, 1280×720, 1440×900 and 1600×1000 give the
    same visible layout on every run, and the saved bytes are unchanged by the forced
    degradation.
  - **T15** Display clamp: a saved 1600×1000 opened on a 1366×768 work area is clamped for the
    session, the saved size is unchanged, and it restores at 1600×1000 on a large display.
  - **T16** No audio effect: with a pattern playing, layout changes leave every audio section
    and processed block bit-identical. The allocation probe shows no allocation, lock or I/O in
    `processBlock`.
- **Per-user file:**
  - **T17** Workspaces file: missing, corrupt (renamed `.bad`), 32-entry limit, duplicate name,
    atomic write. The file is not read on the audio thread.
  - **T18** Applying a user workspace changes only `DLAY`; the project does not reference the
    file.
- **Hosts (manual):**
  - **T19** Cubase and REAPER: a layout change marks the project modified (or not, recorded as
    a finding). Save, close, reopen restores the layout. An old `c7c29bb` project opens with the
    migrated layout. Run at 100/125/150/200 %.
  - **T20** Presets: loading kit, pad, pattern and song presets does not change the layout.
- **Budget:**
  - **T21** Boundary fixture (§10): the largest product state + a 2048-byte `DLAY` + `DROL`
    fits `productStateReserve()`. The measured size is printed by the test.

## 12. Decisions requested from Architecture Guard

1. **Q1** New section `DLAY` v1 instead of `DUIS` v2 or a schema bump, keeping old builds able
   to open new projects (§2).
2. **Q2** Exception: an invalid `DLAY` falls back instead of rejecting the project (§7).
3. **Q3** Real editor size in `DLAY`; `UIST` clamped to the old range. New editor maximum
   2560 × 1600 logical px; minimum 1040 × 700 unchanged (§5).
4. **Q4** Withdrawn after review: no reserve change; boundary fixture T21 instead (§10).
5. **Q5** Per-user workspaces file location, format and limits (§9).
6. **Q6** Mark the host project dirty on committed layout changes via JUCE
   `withNonParameterStateChanged` (§6); host behaviour to be tested (T19).

## 13. Limits of this proposal

- **Byte layout:** written from the code at `c7c29bb`. No implementation exists, and the
  numbers (limits, heights, widths) are proposals to be confirmed against the native UI in
  Gate 2.
- **Host behaviour is untested:** host dirty-flag behaviour, display placement and DPI handling
  in Cubase and REAPER have not been tested.
- **Not covered here:**
  - custom-kit semantic roles (separate Draft PR, option B, issue #128);
  - the Cubase MIDI drag failure (separate task).
