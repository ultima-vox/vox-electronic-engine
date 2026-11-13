# VOX Drums UI/UX v3 — Gate 1 visual proposal

Status: **proposal for owner visual sign-off** (issue #127, spec v3, section 7 Gate 1).
Architecture is the approved spec v3; this folder proposes only the visual language,
the control migration and the interaction details. No C++ is changed. PR #125 and its
Gate B are not touched. Native JUCE implementation starts only after sign-off, in a
separate Draft PR (Gate 2).

Base: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2`.
Data in the renders comes from that commit: pad names/notes, factory kit names,
parameter defaults (`products/drums/parameters.csv`), choice lists, generator settings.

## 1. Deliverables

| Gate 1 item (spec v3 §7) | File |
|---|---|
| 1. Default docking zones and responsive degradation | `screens/00-docking-schematic.png`, `screens/06-beatmaking-{1040x700,1280x720,1600x1000}.png` |
| 2. Beatmaking (Step + dock mixer) | `screens/01-beatmaking.png` |
| 2. Composition (Song + Step) | `screens/02-composition.png` |
| 2. Sound Design (Pad Rack + Full Sound Editor) | `screens/03-sound-design.png` |
| 2. Expanded Mixer with FX | `screens/04-mixing.png` |
| 2. Generator docked beside Step | `screens/05-generator-docked.png` |
| 3. Control migration inventory and interactions | this README §3–§4, `screens/07-states.png` |
| 3. Key states: pinned Inspector, 3 Mixer states, split editors, dock-target previews | `screens/07-states.png` |
| 4. Design tokens and component behavior | `screens/08-tokens-components.png`, §5 |
| 4. Feasibility / compatibility checklist | §7 |

Regenerate: `cd source && python3 gen.py && python3 sheets.py && node shot.js`
(Playwright + Chromium; output in `source/html` and `source/png`).

## 2. Visual decision in one paragraph

Dark, flat workstation using the family surface tokens of `libs/vox-ui/Tokens.h`.
Panels sit on a dark gutter with 6 px gaps and a 30 px header (grip, title or tabs,
actions). Two accent roles only: **cyan** = focus, activity, host transport, active
toggles, playhead, loop; **copper** (Drums accent) = the primary musical action of a
panel (Drag MIDI, Generate Pattern, Load) and the selected tab/segment underline.
Pad identity colours appear only as a thin bar, step fill and meter. Sound shaping,
Pan and sends are 270° rotary knobs with label above and value below; levels are
vertical faders with meters; sample range uses graphical handles on the waveform.
Every state that matters (selected, muted, missing, disabled, fallback routing,
pinned) has text or shape, never colour alone.

## 3. Control migration inventory

Every control of the 8 legacy pages (`DrumsEditor.cpp`, `DrumsPages.cpp`,
`PatternEditor.cpp`) maps to one canonical location. All 174 host parameters keep
their IDs, order, ranges and automation; the UI only rebinds. "Removed" means pure
navigation with no state.

### PERFORMANCE
| Legacy control | Binding | New location |
|---|---|---|
| Page tabs (8 pages) | navigation | Removed: replaced by docked panels and workspace presets |
| 16 pad buttons (select / play) | selection, audition | Pad Rack (Grid or List). Select and audition are separate gestures |
| Kit Volume | `drm.kit.volume` | Mixer: Main strip fader (all states; Collapsed shows its level) |
| Velocity | `drm.kit.velocity` | Settings panel: Velocity knob |
| Choke | `drm.kit.hat_choke` | Settings panel: Choke groups toggle |
| Mode | `drm.seq.mode` (Host MIDI / Internal / Both) | Routing panel: Sequencer |
| Pattern | `drm.seq.pattern` (5 slots) | Top chrome: pattern selector |
| Swing | `drm.seq.swing` | Step Sequencer toolbar: Swing knob |
| Accent | `drm.seq.accent` | Step Sequencer toolbar: Accent knob |
| Dly Fbk, Delay, Rev Size, Reverb | `drm.fx.delay.feedback`, `.delay.level`, `.reverb.size`, `.reverb.level` | Mixer Expanded: FX return strips |
| Song Mode | DSNG song on/off | Top chrome: Pattern / Song |
| Swap A/B | pattern variation | Step Sequencer toolbar: A ⇄ B |
| Load factory kit | KitStore / presets | Library panel: Kits → Load |
| Transport text (Playing, BPM, Song bar) | read-only host info | Top chrome: host status (read-only, "from host") |

### SOUND (per selected pad)
| Legacy control | Binding | New location |
|---|---|---|
| Model, Note, Choke group, Voices | DPAD structural | Sound Editor: Source (Model) and Output (Note, Choke, Voices); Inspector shows model and note |
| Level, Tune, Decay, Pan, Cutoff | `drm.pNN.level/tune/decay/pan/cutoff` | Sound Editor; Inspector Quick Controls (Level, Pan, Tune, Decay); Mixer (Level, Pan) |
| Fine, Character | DPAD | Sound Editor: 2 · Tone |
| Filter type, Resonance | DPAD | Sound Editor: 3 · Filter & transient |
| Attack, Sustain (transient) | DPAD | Sound Editor: 3 · Filter & transient |
| Pitch Env, Pitch Decay, Drive, Sub, Tail | DPAD, Kick model only | Sound Editor: Kick body (advanced, shown only for Kick, as today) |
| Source (Synth / Sample / Layer), Players (A only / Velocity A/B / A + B) | DPAD | Sound Editor: 1 · Source |
| Player A / B, Start, End, Sample level, Split, Loop, Reverse, Normalize | DPAD / DSMP | Sample Editor (central); Start/End as waveform handles with numeric values |
| Load, Clear, Library (copy into library) | sample store | Sample Editor toolbar: Load…, Copy to library, Clear; audio file drop on a pad or the Sample Editor |
| Mute | `drm.pNN.mute` | Pad Rack, Step lane, Mixer, Inspector (same parameter) |
| Send 1 / Send 2 | `drm.pNN.send1/send2` | Sound Editor: 4 · Output (Delay send, Reverb send); Mixer Expanded |
| Output (Main, Out 2…9) | DPAD output | Sound Editor: Output; Routing matrix; Mixer Expanded strip footer |

### PATTERN/SONG
| Legacy control | Binding | New location |
|---|---|---|
| Step grid | DSEQ | Step Sequencer (central) |
| Steps per bar 16/32/64, bars 1–4 | DSEQ | Step Sequencer toolbar: steps, bars |
| Bar selector | UI (DUIS patternBar) | Step Sequencer toolbar: BAR 1–4 |
| Lanes 1–8 / 9–16 | navigation | Removed as a mode: all 16 lanes visible; vertical scroll only below the minimum height |
| Dup (bar → next), Clear (bar), Seed | DSEQ | Step Sequencer toolbar: Bar ▾ menu |
| A/B, A>B | DSEQ | Step Sequencer toolbar: A/B, A ⇄ B, A → B |
| Undo / Redo | pattern undo | Top chrome: Undo / Redo |
| MIDI button (click: save; drag: DAW) | PatternExport | Step Sequencer: **Drag MIDI** handle (drag only) + ▾ **Save MIDI file…** (existing save code kept) |
| Velocity, Chance, Ratchet, Micro, Accent (selected step) | DSEQ step | Inspector (selection) and Parameter Lanes under the step ruler |
| Step lock | DSEQ step `locked` | Inspector: Lock; lock mark on the step cell |
| Lane on/off | `drm.seq.lane.pNN` | Step Sequencer lane header: lane dot |
| Song on/off | DSNG | Top chrome: Pattern / Song |
| Song length 16/32/64 | DSNG | Song Arranger toolbar |
| Slot P1–P5, A/B, Place | DSNG | Song Arranger toolbar |
| Insert, Delete, Dup, Clear, Copy, Paste, Make Unique, Loop, Loop = selection | DSNG | Song Arranger toolbar |
| Edit Bar toggle | DSNG copy-on-write | Song Arranger: **Edit Source Pattern / Edit This Bar** (large segmented control with linkage text) |
| MIDI scope (Pattern / Entire Song) | DUIS exportScope | Removed as a setting: scope fixed by the handle (Step Sequencer = pattern, Song Arranger = entire song) |

### MIXER
| Legacy control | Binding | New location |
|---|---|---|
| 16 × Level, Pan, Send 1, Send 2, Mute, Solo, meter | `drm.pNN.*` | Mixer dock. Compact: fader, meter, Pan, M/S. Expanded: + Delay/Reverb sends, peak hold, output and fallback |
| Delay Time, Feedback, Tone, Return | `drm.fx.delay.*` | Mixer Expanded: Delay return strip |
| Reverb Size, Damp, Return | `drm.fx.reverb.*` | Mixer Expanded: Reverb return strip |
| Main fader, Main L/R meter | `drm.kit.volume` | Mixer: Main strip; Collapsed status line |

### GENERATE
| Legacy control | Binding | New location |
|---|---|---|
| Style (7), Density, Groove, Syncopation, Swing, Humanize, Complexity, Fills, Variation, Seed | DGEN | Generator panel |
| Lock lanes, Lanes to mutate | DGEN | Generator panel: Locked lanes; Mutate lanes… chooser |
| Generate Pattern, Fill, Variation A > B, Mutate Lanes | generator actions | Generator panel: target block "Pattern n X · unlocked steps" |
| Generate Song | generator action | Generator panel: separate "whole song" block, asks to confirm |
| Undo / Redo | shared undo | Top chrome |
| Result message | UI | Generator panel status line |

### ROUTING
| Legacy control | Binding | New location |
|---|---|---|
| Per-pad note | DPAD note | Routing panel (matrix rows); Sound Editor Output |
| Per-pad output Main / Out 2…9 | DPAD output | Routing matrix; host-disabled outputs dashed; fallback "Out n→Main" in amber |
| Sequencer mode | `drm.seq.mode` | Routing panel header |

### PRESETS
| Legacy control | Binding | New location |
|---|---|---|
| Type (Kit / Pad / Pattern / Song), list | preset IO | Library panel: Kits / Pads / Patterns / Songs, factory and user groups |
| Name for Save, Save | preset IO | Library: Save… (name field in its dialog) |
| Load, Undo Load | preset IO | Library: Load, Undo Load |
| Import Kit…, Export Kit… (.voxkit) | portable kit | Library: Import .voxkit, Export .voxkit |
| Missing-sample message | sample store | Library warning row; pad tile "● missing" |

### ADVANCED
| Legacy control | Binding | New location |
|---|---|---|
| Voice limit (16 / 32 / 64) | engine | Settings panel |
| Velocity, Choke groups | `drm.kit.velocity`, `drm.kit.hat_choke` | Settings panel |
| Panic | processor | Top chrome and Settings panel |
| Diagnostics text (voices, dropped MIDI, sample memory, rate/block, outputs, version/schema) | read-only | Settings panel: Diagnostics (read-only) |

Nothing is added beyond these controls, except layout commands (dock, split,
maximize, Mixer state, Workspace and Panels menus) that the spec requires.

## 4. Interactions

### Selection and audition
- One selected pad, step set and song bar (UI state). Pad Rack, Step lanes, Mixer
  strip and Sound Editor highlight the same pad.
- Click on a pad body selects without sound. Audition: ▶ corner of the tile or Enter
  while the Pad Rack has focus. Hit flash shows playback, independent of selection.
- Step: click toggles; drag paints; Shift-drag selects a range; Ctrl/Cmd-click adds.
  Selected steps get a white outline; the Inspector shows them.

### Inspector
- Follows the selection: pad → Quick Controls (Level, Pan, Tune, Decay, M/S, model,
  note, output, Open Sound Editor); steps → exact values or "mixed" ranges; song bar →
  linkage and loop.
- Pin keeps the current object; a banner names it and what is selected elsewhere.
- If the pinned object disappears after a load (shorter pattern or song), the panel
  shows "unavailable" with Unpin and Pin current selection. Nothing is edited
  until the user chooses.

### Step Sequencer and parameter lanes
- Toolbar: BAR, Bar ▾ (Duplicate to next bar, Clear bar, New seed), steps, bars, A/B,
  A ⇄ B, A → B, Swing, Accent, Drag MIDI.
- Parameter lanes (Velocity, Chance, Ratchet, Microtiming) sit under the same ruler
  and scroll/zoom with the grid. Each lane has a selector; 2 lanes at ≥ 860 px window
  height, 1 below. Drag a bar to set a value; drag across bars to draw; double-click =
  default. Edits apply to the current lane's steps (or the selection when one exists).
- Mass edits that are not supported are shown disabled with the reason.

### Song Arranger
- The edit target is a large segmented control with text that names the bars an edit
  reaches. Edit This Bar makes the bar unique on the first real edit only; Undo
  restores the link.

### Drag MIDI
- The handle starts an OS file drag of a temporary `.mid` (pattern in the Step
  Sequencer, entire song in the Song Arranger). Clicking it does not open a dialog.
- ▾ → Save MIDI file… keeps the existing save path as a separate command.
- After a drag the handle says "Dragged · P2 A" for 3 s. The plugin cannot see whether
  the host accepted the drop, so no success is claimed.
- The Cubase failure reported at Gate B is not explained yet; see §7.

### Docking
- Drag a panel header: valid targets highlight in cyan (zone = tab, editor half =
  split), invalid targets do not and the ghost states the reason. Esc cancels.
- Split ▾ in an editor header: Below / Right / Close split. Divider drag; double-click
  = 50/50. Maximize (⤢ or header double-click) folds side zones to rails and the Mixer
  to Collapsed; Restore returns the previous layout exactly.
- Mixer: state switch in its header; drag its top edge for a custom height within the
  state's range; never overlays the work area.

### Keyboard and focus
- The editor handles a key only when one of its components has keyboard focus; every
  unhandled key is passed on (`keyPressed` returns false), so host transport keys keep
  working. Space is never consumed.
- Tab / Shift-Tab move within a panel; F6 / Shift-F6 move between panels in reading
  order (chrome → left zone → central editors → right zone → Mixer). Focus ring =
  2 px cyan.
- Arrows move the selection in the focused Pad Rack or step grid; Enter auditions the
  selected pad; Esc cancels a drag or closes a menu.

## 5. Tokens and components

- Surfaces and text: family tokens from `libs/vox-ui/Tokens.h`, unchanged.
- Proposed Drums semantic layer (no hard-coded product colours in panels):
  `accent.primary` = copper `#EF805D`, `accent.focus` = cyan `#00DDF5`,
  `state.danger` (fills `#E4425D`, text `#FF8FA0`), `state.ok`, `state.warning`,
  `pad.1…pad.16`.
- Contrast on panel `#0B1925`: family textMuted `#587487` = 3.6:1 (too low for 10–11 px
  text). Proposal: `#6F8A9D` (4.9:1). This is a family token; changing it affects every
  VOX product, so it needs the design-system owner, otherwise a Drums-local token.
- Components and states (default, hover, pressed, focused, disabled, primary, danger,
  on; knob default/hover/drag/bipolar/disabled; fader + meter with peak; step cell
  states; panel header normal/focused/pinned) are on `08-tokens-components.png`.
- Knob: vertical drag, Shift = fine, double-click = default, wheel = one step; host
  automation moves it live; gestures begin/end around a drag for correct automation.

## 6. Layout persistence (proposal; needs Architecture Guard review in Gate 2)

- The layout is UI-only state: zones, tabs, split direction and ratio, maximized
  editor, Mixer state and height, Pad Rack view, parameter lanes, Inspector pin.
- Versioned and bounded: `DUIS` v1 → v2 extension (append-only fields, length-checked,
  unknown values → safe default). Kit, pattern, song and parameter state are not
  touched; workspace presets stay separate from kit/pattern/song presets.
- Deterministic migration from DUIS page 0–7: PERFORMANCE → Beatmaking; SOUND → Sound
  Design; PATTERN/SONG → Composition if Song is on, else Beatmaking; MIXER → Mixing;
  GENERATE → Beatmaking with the Generator tab active; ROUTING → Mixing with Routing
  active; PRESETS → Beatmaking with Library active in the left zone; ADVANCED →
  Beatmaking with Settings active. Selected pad, song bar and pattern bar carry over.
  `exportScope` is read and ignored (scope is fixed by the handle).
- Named user workspaces: stored outside the project in a user settings file, written
  on the message thread only. Invalid or unreadable layout → Beatmaking default.
- Required before merge: round-trip, old-project load, unknown-section and
  project-portability tests (spec v3 §5).

## 7. Feasibility and compatibility checklist

| Item | Status |
|---|---|
| Docking in JUCE | No built-in dock manager in JUCE. Needs an in-editor dock layer (components, header drag, drop-target overlay, splitters). Feasible; largest implementation risk is effort and layout tests, not APIs. |
| One instance per panel; no OS windows | By design: panels are child components of the editor. |
| MIDI drag to DAW | Current code already offers drag (PatternExport + JUCE external file drag). It failed in Cubase at Gate B; cause **unknown** (не знаю). P1: reproduce on Windows/Cubase, compare with REAPER, then fix inside Gate 2. Success only with editable notes in both hosts. |
| "Insert into track" | Not offered: VST3 has no API to create a part on a host track. |
| Save MIDI file | Existing code kept behind ▾; not a new feature. |
| Host parameters | 174 IDs, order, ranges, automation unchanged; controls only rebind. |
| DSP, buses, ABI, preset/pattern/song formats | Unchanged. 9 stereo host buses unchanged. |
| State | Only DUIS (UI section) changes, additively, with migration tests; unknown-section policy unchanged. |
| Realtime | Meters, hit flash and playhead read lock-free snapshots on a ~30 Hz UI timer; no allocation, locks or I/O added to the audio callback. |
| Minimum size | 1040×700 works (one editor, rails, Mixer collapsed). Host resize limited to it. |
| DPI | Layout in logical px; JUCE scale factor from the host. 125/150/200 % evidence is a Gate 2/3 deliverable. |
| Keyboard | No global key capture; unhandled keys pass to the host. |
| Font | Renders use Inter; JUCE uses the platform font (Segoe UI on Windows). Metrics differ slightly; layout keeps slack. |
| Family tokens | Copper accent and muted-text change need the design-system owner (shared `libs/vox-ui`). |
| Generator genre fidelity | Functional work in issue #128; the UI only shows the real style setting and target. |

## 8. Limits of this proposal

- Renders are HTML/Chromium at 2× of the logical size, not JUCE output; no host was
  used. Values shown are factory defaults or illustrative pattern data.
- Interaction details (keys, gestures) are proposals for Gate 2 and will be tested
  there; nothing here is verified in a DAW.
