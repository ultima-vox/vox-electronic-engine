# VOX Drums UX/UI v2 — Stage 0 design proposal (issue 127)

**Status:** design only, awaiting the owner's visual approval. No product code is changed.
Grounded on HEAD `c7c29bb` (PR 125). PR 125 and Gate B are not touched.

The screens are **reference renders**, produced from `source/gen.py` (HTML) and
`source/shot.js`. They are not product code; the product UI stays native JUCE
(custom `LookAndFeel`, painted components, no web view). Values, names and states in
the screens are those of the real product; the audio and state layers do not change.

## 1. Screens

| # | Screen | File |
|---|---|---|
| 1 | Sequence — Pattern (default workspace) | `screens/01-sequence-pattern.png` |
| 2 | Sequence — Song, 64 bars, editing a song bar | `screens/02-sequence-song.png` |
| 3 | Play / Kit with the Sound inspector | `screens/03-play-kit-sound.png` |
| 4 | Mix with the Routing drawer open | `screens/04-mix-routing.png` |
| 4b | Mix with returns and Main (drawer closed) | `screens/04b-mix-returns.png` |
| 5 | Library (annotated wireframe) | `screens/05-library.png` |
| 6 | Generate drawer over Sequence | `screens/06-generate-drawer.png` |
| 7 | Settings and diagnostics drawer | `screens/07-settings-drawer.png` |
| 8 | Tokens and component states | `screens/08-tokens-states.png` |
| 9 | Sequence at 1280 × 720 (compact) | `screens/09-sequence-compact-1280x720.png` |

Canvas 1440 × 900 logical px, rendered at 2×. Numbered markers refer to the notes at the
bottom of each screen.

States shown: normal and selected (every screen); playing vs stopped (chrome, ruler,
timeline); muted lane/pad/strip (lane 12); empty and unique song bars; disabled button
(Paste, Redo); missing sample (pad 10, Library status); output assigned but not enabled
by the host (strip 8, pad chips, Routing drawer); empty list (Library); compact (screen 9).

## 2. Structure

**Chrome (always visible):** brand; Pattern/Song (the playback mode, a real state);
host status, tempo, signature and position (read-only: the plugin does not control the
host transport, so there is no Play button); enabled outputs; Undo, Redo; Panic.

**Workspaces:** SEQUENCE (default), PLAY / KIT, MIX, LIBRARY.

**Drawers (from any workspace, right side, 440–600 px):** Generate, Routing, Settings.
One at a time; Esc or Close dismisses; the workspace under it stays live.

**Selection is global:** selecting a pad (pad, lane header or strip) selects it
everywhere. The saved editor state keeps workspace, pad, song bar and pattern bar (the
existing DUIS section: its `page` values 0–7 map to the four workspaces, see §6).

## 3. Control inventory: where every current control goes

Host parameters keep their IDs and attachments. "Structural" = `KitStore` setting.

### PERFORMANCE page (current)
| Control | Binding | New place |
|---|---|---|
| 16 pad buttons (select + audition, hit flash) | `audition()`, hit mask | Play/Kit pad grid; hit flash also on lane headers and strips |
| Volume, Velocity knobs | `drm.kit.volume`, `drm.kit.velocity` | Play/Kit Macros; volume also Mix › Main fader; velocity also Settings |
| Swing, Accent knobs | `drm.seq.swing`, `drm.seq.accent` | Sequence groove row; Play/Kit Macros |
| Delay feedback/return, Reverb size/return macros | `drm.fx.*` | Play/Kit Macros; Mix › FX returns |
| Mode combo | `drm.seq.mode` | Sequence groove row ("Seq:"); Routing drawer |
| Pattern combo | `drm.seq.pattern` | Sequence toolbar (Pattern) |
| Song Mode toggle | DSNG `songMode` | Chrome Pattern/Song |
| Swap A/B | pattern store | Sequence toolbar |
| Choke toggle | `drm.kit.hat_choke` | Settings drawer |
| Transport text | read-only | Chrome |
| Load factory kit | `Presets::loadFactoryKit` | Play/Kit header; Library › Kits |

### SOUND page
| Control | Binding | New place |
|---|---|---|
| Pad selector | selection | removed: the pad grid is next to the inspector; ‹ › step through pads |
| Model, Note, Choke, Voices | structural | Sound inspector › Sound (Note also Routing drawer) |
| Level, Tune, Decay, Pan, Cutoff | `drm.pNN.*` | Sound inspector (Level/Pan also Mix strip) |
| Mute | `drm.pNN.mute` | pad, lane header, strip |
| Fine, Character | structural | Sound inspector › Sound |
| Filter, Resonance, Attack, Sustain | structural | Sound inspector › Filter and transient |
| Pitch env, Pitch decay, Drive, Sub, Tail | structural | Sound inspector › Kick body (Kick model only, as today) |
| Source, Players mode, A/B | structural | Sound inspector › Sample |
| Load, Clear, Library | `SampleLibrary` | Sound inspector › Sample ("To library") |
| Waveform, status | sample info | Sound inspector › Sample |
| Start, End, Level, Split; Loop, Reverse, Normalize | structural | Sound inspector › Sample |
| File drop on the editor | `loadSample` | unchanged (drop on a pad or the inspector) |

### PATTERN/SONG page
| Control | Binding | New place |
|---|---|---|
| Song: mode, length, slot, variation, Place, Ins, Del, Dup, Clear, Copy, Paste, Unique, Loop, Loop=Sel | `editSong`, `songedit` | Sequence (Song) toolbar; mode in the chrome |
| Edit Bar toggle | `editSongBar` | Sequence breadcrumb: "Edit this bar / Edit source pattern" |
| MIDI scope combo | DUIS `exportScope` | Sequence groove row: MIDI Pattern / Entire Song |
| Timeline (select, drag-select, playhead, loop) | song store | Sequence (Song) timeline |
| Pattern combo | `drm.seq.pattern` | Sequence toolbar |
| 16 lane switches | `drm.seq.lane.*` | lane headers |
| Bar 1–4, steps, bars, Dup, Clear, Seed | pattern store | Sequence toolbar (compact: "Bar actions" menu) |
| Lane page 1–8 / 9–16 | view | removed: 16 lanes always fit (scroll below the minimum height) |
| Grid (toggle, select) | pattern store | Sequence grid |
| Velocity, Chance, Ratchet, Micro, Accent, Lock | step | step inspector (sliders) |
| A/B, A>B, Undo, Redo | history | toolbar (A/B), chrome (Undo/Redo) |
| MIDI button (click = save, drag = drop) | export snapshot | **two controls:** "Save .mid…" and the "Drag to DAW" handle |

### MIXER page
| Control | Binding | New place |
|---|---|---|
| Per pad Level, Pan, Dly, Rev, M, S | `drm.pNN.level/pan/send1/send2/mute/solo` | Mix strip (level = fader; others sliders/toggles) |
| Pad meter, output label | meters, store + host | Mix strip |
| Delay time, feedback, tone, return; Reverb size, damp, return | `drm.fx.*` | Mix › FX returns |
| Main knob, Main L/R meter | `drm.kit.volume`, meter | Mix › Main |

### GENERATE page → Generate drawer
Style, eight amounts, seed, 16 lane locks, 16 mutate targets, Generate Pattern, Generate
Song, Mutate Lanes, Fill, Variation A>B, status: all kept. Undo/Redo move to the chrome.

### ROUTING page → Routing drawer
Per pad note and output, per pad state text, sequencer mode, sync text, enabled outputs:
all kept, as a matrix.

### PRESETS page → Library workspace
Type, list (factory + user), name, Load, Save, Undo Load, Import Kit, Export Kit, status,
sample diagnostics: all kept.

### ADVANCED page → Settings drawer
Voice limit, velocity, choke, Panic (also in the chrome), diagnostics text: all kept.

## 4. Proposed affordances the product does not support today

Each needs a decision; none is assumed.

| Affordance | Why it is not free | Proposal |
|---|---|---|
| Loaded kit name in the chrome | the state stores no kit name | not drawn. Adding it needs a state field (schema) — out of a UI-only change |
| "Export to a MIDI channel" as a button | a VST3 plugin cannot create a clip on a host track; the only routes are OS drag-and-drop of a file, or MIDI output recorded by the host | keep drag-and-drop, made a separate, obvious handle. MIDI output would change the plugin's bus configuration — not UI-only |
| Confirmation before Generate Song | new dialog | proposed (one Undo already restores everything) |
| Copy diagnostics | new, text to clipboard | proposed, needs approval |
| Mixer banks 1–8 / 9–16 | view only | proposed for widths under ~1360 px |
| Search, tags, favourites, rename, delete, kit artwork | not stored / backlog 126 | not drawn |
| Play/Stop/Record, tempo edit | the plugin does not control the host | not drawn |
| More generator styles or factory presets (Dark Psy, Uplifting Trance…) | new content and generator styles, not UI | outside this issue; backlog 126. The Library and style list need no layout change to hold them |
| Per-pad ADSR, pitch/FX tabs, Bus A/B, layers beyond A/B (seen in the reference pictures) | no such DSP | not drawn |

## 5. The drag-and-drop defect (Gate B finding)

In Cubase the owner could not drag from the MIDI button; only the save dialog appeared.
Decision: fixed within this redesign, not in PR 125. What is known: the automated tests
cover the file a drag produces, never a real drop into a host. The cause is **not
established**. The redesign removes the dual-purpose button (click = dialog, drag =
drop), which is the first suspect because a drag that is not recognised falls through to
the click. Stage 1 must reproduce the failure on Windows before claiming a fix, and
Stage 2 must show a real drop in Cubase and REAPER.

## 6. Engineering notes for Stage 1 (no change proposed here)

- Parameter IDs, ranges, automation, state sections, MIDI export bytes, buses: unchanged.
- DUIS `page` (0–7) stays as stored. Mapping on restore: 0→Play/Kit, 1→Play/Kit,
  2→Sequence, 3→Mix, 4→Sequence + Generate drawer, 5→Mix + Routing drawer, 6→Library,
  7→Settings drawer. Written values stay within 0–7, so older builds read newer projects.
  This default changes what a restored project shows first; it needs the owner's nod.
- Existing test selectors (`sliderFor`, `buttonFor`, `comboFor`, `structuralSlider`,
  `pageComponent(n)->named(...)`, `songStrip()`, `patternEditor()`) are kept or mapped
  one-to-one in the tests, with the mapping listed in the PR.
- Selection, hit flash and meters keep using the existing thread-safe accessors.

## 7. Tokens

| Token | Value |
|---|---|
| Background | `#101318` |
| Surface 1 / 2 / line | `#1C232C` / `#242D38` / `#2E3845` |
| Text 1 / 2 / 3 | `#F0F2F4` / `#A9B3BF` / `#74808D` |
| Primary action (amber) | `#FFAA3B`, text on it `#1B1305` |
| Transport / connection / send 1 (cyan) | `#32C5D2` |
| Send 2 / reverb (violet) | `#9B7CE8` |
| Error, clip, missing (red) | `#F0584C` |
| Pad colours 1–16 | `#FF8A3B #9B7CE8 #F06BA8 #32C5D2 #4C9BF0 #7FD66B #D6C24A #E8B04A #5FD1A8 #5FD1A8 #8FCB5A #8FCB5A #C9A26B #C9A26B #7FB6E8 #7FB6E8` |
| Spacing | 4 (micro), 8, 12, 16, 24; outer padding 20; panel gap 16 |
| Radius | 8 (panels, controls), 4 (step cells), 11 (chips) |
| Control height | 32 (in panels), 36 (chrome, primary); hit target ≥ 32 × 32 |
| Type | 12 tertiary/labels caps, 13–14 body, 15–16 values and buttons, 16–18 panel titles, 22 brand |
| Numbers | always with units: %, st, ct, ms, kHz, ×, vel; notes as name + number, e.g. `C1 (36)` |

Contrast (computed, WCAG 2 formula): text 1 on surface 1 = 14.1:1; text 2 = 7.5:1;
text 3 = 3.9:1 on surface 1 and 4.6:1 on the background (below AA for body text on
surfaces, so it is used only for labels and hints, never for values); amber button
text = 9.7:1. Pad colours are never the only cue: lanes and strips
also carry the number and name.

Controls: sliders for everything linear; knobs are not used in these screens (the issue
asks to reserve them; none of the current parameters needs one). Mixer level is a
**vertical** full-length fader, as in the reference pictures; the issue text says
"horizontal" — to be confirmed.

## 8. Measurements and responsiveness

| Element | 1440 × 900 | 1280 × 720 | Rule |
|---|---|---|---|
| Chrome / workspace bar | 56 / 48 | same | fixed |
| Lane header | 236 | 196 | name never truncated below 196 |
| Step cell (16 steps) | ≈ 72 × 30 | ≈ 64 × 21 | min 16 px high; below that the grid scrolls |
| Step cell (32 / 64 steps) | ≈ 36 / 18 wide | ≈ 32 / 16 | 64 steps: horizontal scroll under 1100 px of grid |
| Timeline bar | ≈ 84 × 34 | ≈ 74 × 30 | 16 per row |
| Pad | ≈ 240 × 124 | ≈ 200 × 96 | 4 × 4 |
| Sound inspector | 640 | 520 (sections scroll) | fixed width, scrolls vertically |
| Mixer strip | ≈ 71 | banks of 8 at ≈ 130 | bank switch under ~1360 px |
| Drawer | 440 (Generate, Settings), 600 (Routing) | 400 / 560 | over the right side |

Minimum editor size proposed: 1280 × 720 (today 1040 × 700). **This raises the minimum**;
if 1040 must stay, Sequence needs lane-header 160 and a "Bar actions" menu, and Mix
always uses banks. Scaling 100/125/150/200 % is by the host/JUCE scale; all sizes above
are logical px.

## 9. Keyboard

| Key | Action |
|---|---|
| 1–4 | workspace |
| G / R / , | Generate / Routing / Settings drawer; Esc closes |
| Arrows | move the step cursor (Sequence), the pad (Play/Kit), the strip (Mix) |
| Space / Enter | toggle the step; audition the pad |
| Shift + arrows | extend the timeline selection |
| A | accent, L lock on the selected step |
| Ctrl+Z / Ctrl+Y | undo / redo |
| Ctrl+C / Ctrl+V | copy / paste song bars |
| Double-click a slider | reset; Shift-drag fine; type-in on double-click of the value |

Hosts may keep some keys for themselves; every action also has a visible control. The
plugin does not take keyboard focus unless a control is clicked (as today).

## 10. Audit against `DAW_ACCEPTANCE_CHECKLIST.md`

| Section | Effect of the redesign |
|---|---|
| A Load | none |
| B Outputs | routing moves to a drawer; "plays on Main" stays explicit on strip, pad and matrix |
| C Mixer | same controls; faders instead of knobs; returns and Main separated |
| D Song | same operations; breadcrumb makes "which bar am I editing" explicit |
| E MIDI export | **changes:** separate Save and Drag controls; must be re-verified in both hosts (§5) |
| F Generator | drawer over the grid: result visible while generating |
| G Presets | Library; same operations and messages |
| H State | restored workspace mapping (§6) |
| I Editor | new minimum size (§8); all scales to be re-screenshotted |
| J Performance | none (UI only) |
| K Sound | none |

## 11. Decisions requested from the owner

1. Approve the four workspaces + three drawers and the chrome as drawn.
2. Minimum size 1280 × 720 (or keep 1040 × 700 with the compromises in §8).
3. Vertical mixer faders (vs the "horizontal" wording of the issue).
4. Restored-workspace mapping in §6.
5. The proposed new affordances in §4: Generate Song confirmation, Copy diagnostics,
   mixer banks.
6. Export: drag-and-drop handle + Save button; no "export to channel" button, because
   the plugin cannot place a clip on a host track by itself.
