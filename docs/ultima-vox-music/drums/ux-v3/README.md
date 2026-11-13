# VOX Drums UI/UX v3 — Gate 1 visual proposal (revision 3.2)

Status: **v3 concept approved with targeted visual changes** (owner decision on issue #127,
2026-11-13). Revision 3.2 applies the TL review of v3.1 (comment 6861782128) for **final
visual sign-off**. Native JUCE implementation stays on HOLD until then (spec v3, section 7,
Gate 1).
Architecture is the approved spec v3; this folder proposes only the visual language,
the control migration and the interaction details. No C++ is changed. PR #125 and its
Gate B are not touched. Native JUCE implementation starts only after sign-off, in a
separate Draft PR (Gate 2).

Base: `drm/production-candidate` @ `c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2`.
Data in the renders comes from that commit: pad names/notes, factory kit names,
parameter defaults (`products/drums/parameters.csv`), choice lists, generator settings.

## 0. Change log 3.2 (against `52c2be2`)

Architecture, palette, panels, presets and 3.1 decisions (banks, hit targets, lane rule) are
unchanged. Before/after crops and 100 % crops: `screens/09-v32-changes.png`.

1. **Pad Rack Grid, P1.**
   - Each of the 16 equal squares is now a raised rubber **face** (about 2/3 of the pad) for
     playing, above a flat **name bar** for selecting without sound. M/S sit in the name bar.
   - Number and note are in the face corner; the measured peak bar runs along the face bottom.
   - Pressed: the face travels 1.5 px into the housing. Selected: white ring and a light name
     bar.
   - Hit glow is marked `v*` (illustrative velocity until telemetry exists, §6). A hint line
     under the grid explains the two gestures.
   - Left zone: 404 px at 1440×900 (pads 92 px).
2. **Overload semantics, P1.**
   - The Main lamp is now **OVER (≥ 0 dBFS, pre-host)**, latched; the −1 dB lamp stays.
   - **No CLIP lamp anywhere**: the product has no clipping or saturation event to report,
     and a lamp at a float threshold would claim more than is measured.
   - Measurement points are listed in §6.
3. **Knob hierarchy, P2.**
   - Sound Editor primary controls (Tune, Decay, Drive, Cutoff, Level) are 52 px knobs with
     an 11-step tick scale and a centre detent mark for bipolar knobs.
   - Secondary trims (Fine, Character, Reso, transient, Pitch env/dec, Sub, Tail, Pan, sends)
     are 34 px.
4. **Faders, P2.**
   - The track is neutral: the setting is not loudness, so no colour scale; the meter beside
     it shows measured audio.
   - Added a % scale and a larger 30 × 18 metal cap.
   - Strips show a numeric peak; Main shows L/R meters and per-channel peaks.
   - Interaction: Shift = fine, double-click = default, and a value field where the existing
     binding offers text entry.
5. **Telemetry contract, P2** (§6): aggregation and reset semantics, consumers, tests.

## 0.1 Change log 3.1 (against `843ed0f`)

Architecture, palette, panels and workspace presets are unchanged. Changes:

1. **One tactile control system** for every live control: matte knob bodies on a recessed
   bezel with a separate value arc and a light pointer; level faders with a recessed slot,
   intensity fill and metal cap; raised buttons/toggles with pressed (inset) state and lit
   LED for "on"; recessed meters; rubber pads. Normal, hover, keyboard focus, pressed,
   active and disabled states are defined for each (`08-tokens-components.png`). Material
   tokens are shared, for Drums, Synth and FX (§5).
2. **Pad Rack Grid**: 16 equal square rubber pads (≥ 80 px; below that the zone uses List).
   Top strip = number and note (select without sound); strike surface = audition; name
   centred; M/S compact and uniform; measured peak bar on the right edge; selected (white
   ring outside the pad), muted, solo, missing and OVER states keep the geometry. Routing
   details moved out of the pad (Inspector, List, Mixer).
3. **Intensity scales**: amount controls (level, velocity, drive, sends, feedback, …) use
   a green → yellow → orange → red arc; value controls (cutoff, decay, tone, size, …) a
   neutral steel arc with units; bipolar controls (pan, tune, transient, microtiming) a steel
   arc from the centre detent. Velocity lane bars use the amount scale.
4. **Overload is separate from every gradient**: measured meters (green/yellow/orange,
   no red), a −1 dB near-threshold lamp, a latched **OVER** lamp for pad post-fader peaks
   ≥ 0 dBFS and a latched **CLIP** lamp for the plugin's Main output (replaced in 3.2 by Main OVER; no CLIP lamp), with Reset peaks. What
   each indicator measures is listed in §6.
5. **Mixer viewport**: all 16 strips only when each gets its minimum width (Compact 80 px,
   Expanded 108 px); otherwise a bank of 8 (1–8 / 9–16); below 8 a horizontal scroll.
   1600 px Compact shows 16; 1440 px shows a bank of 8. Mixer M/S hit areas are ≥ 32 × 32 px.
6. **Hit targets**: ≥ 32 px for regularly used controls; dense step grid and pad M/S are the
   documented exceptions with keyboard and alternative-panel equivalents (§5). Lane M/S
   buttons removed from the step lane headers (Pad Rack, Mixer and Inspector keep them).
7. **Parameter-lane rule fixed** (was contradictory): by Step editor height — 2 lanes at
   ≥ 560 px, 1 at 420–559 px, hidden below 420 px. 1040×700 (608 px) shows 2; 1280×720
   (508 px) shows 1; Mixing (398 px) shows none.
8. **Legibility**: labels and values 12 px, captions 11 px, 10 px only inside lamps;
   `screens/01-beatmaking@1x.png` is a 100 % render.
9. Beatmaking: left zone 392 px (Pad Rack Grid with Library stacked below), right zone
   280 px, Mixer Compact 184 px. Sound Design: left 380 px, right 250 px.

## 1. Deliverables

| Gate 1 item (spec v3 §7) | File |
|---|---|
| 1. Default docking zones and responsive degradation | `screens/00-docking-schematic.png`, `screens/06-beatmaking-{1040x700,1280x720,1600x1000}.png` |
| 2. Beatmaking (Step + dock mixer) | `screens/01-beatmaking.png`, 100 % view `screens/01-beatmaking@1x.png` |
| 2. Composition (Song + Step) | `screens/02-composition.png` |
| 2. Sound Design (Pad Rack + Full Sound Editor) | `screens/03-sound-design.png` |
| 2. Expanded Mixer with FX | `screens/04-mixing.png` |
| 2. Generator docked beside Step | `screens/05-generator-docked.png` |
| 3. Control migration inventory and interactions | this README §3–§4, `screens/07-states.png` |
| 3. Key states: pinned Inspector, 3 Mixer states, split editors, dock-target previews | `screens/07-states.png` |
| 4. Design tokens and component behavior | `screens/08-tokens-components.png`, §5 |
| v3.1 → v3.2 differences and 100 % crops | `screens/09-v32-changes.png`; full 1× renders `screens/0{1,3,4}-*@1x.png` |
| 4. Feasibility / compatibility checklist, telemetry and overload | §6, §8 |

Regenerate: `cd source && python3 gen.py && python3 sheets.py && node shot.js`
(Playwright + Chromium; output in `source/html` and `source/png`).

## 2. Visual decision in one paragraph

Dark workstation using the family surface tokens of `libs/vox-ui/Tokens.h`, with
restrained hardware-like controls (matte bodies, subtle bevel, recessed tracks, one light
direction from the top left; no gloss, no decorative 3D, no animation beyond hit flashes).
Panels sit on a dark gutter with 6 px gaps and a 30 px header (grip, title or tabs,
actions). Two accent roles only: **cyan** = focus, activity, host transport, active
toggles, playhead, loop; **copper** (Drums accent) = the primary musical action of a
panel (Drag MIDI, Generate Pattern, Load) and the selected tab/segment underline.
Pad identity colours appear only as a thin bar, LED and step fill. Sound shaping,
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
| Mute | `drm.pNN.mute` | Pad Rack (Grid and List), Mixer, Inspector (same parameter); Step lane header shows "muted" |
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
| Lane on/off | `drm.seq.lane.pNN` | Step Sequencer lane header: lane LED |
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
- Pad Rack Grid, two visible zones per pad:
  - **Face** (raised rubber, about 2/3 of the pad): mouse-down auditions (velocity 100,
    Shift = 127) and selects. The face travels down while pressed.
  - **Name bar** (flat): click selects without sound. Hover highlights the bar; tooltip
    "click to select without sound". M/S buttons sit at its right end.
  - A hint line under the grid repeats the two gestures.
  - Keyboard: arrows move the selection without sound, Enter auditions.
  - List, step lanes and mixer strips select without sound.
- Hit flash shows playback independently of selection. Once telemetry exists (§6), the glow
  and top LED show the strongest velocity since the last frame. Until then the flash has a
  fixed brightness, and the renders mark velocity as `v*` (illustrative).
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
  and scroll/zoom with the grid. Each lane has a selector. Number of lanes by Step editor
  height: 2 at ≥ 560 px, 1 at 420–559 px, hidden below 420 px (selector stays in the
  toolbar). Velocity bars use the amount colour scale. Drag a bar to set a value; drag across bars to draw; double-click =
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
- The Cubase failure reported at Gate B is not explained yet; see §8.

### Docking
- Drag a panel header: valid targets highlight in cyan (zone = tab, editor half =
  split), invalid targets do not and the ghost states the reason. Esc cancels.
- Split ▾ in an editor header: Below / Right / Close split. Divider drag; double-click
  = 50/50. Maximize (⤢ or header double-click) folds side zones to rails and the Mixer
  to Collapsed; Restore returns the previous layout exactly.
- Mixer: state switch in its header; drag its top edge for a custom height within the
  state's range; never overlays the work area. Strips: all 16 when each gets its minimum
  width (Compact 80 px, Expanded 108 px), else a bank of 8 (1–8 / 9–16), else horizontal
  scroll; FX returns and Main stay visible. "Reset peaks" clears OVER latches and peak holds.

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
- Shared semantic roles, opt-in through `libs/vox-ui` (no global change to other products,
  no hard-coded product colours in panels): `accent.product` (Drums = copper `#EF805D`),
  `accent.focus` (cyan `#00DDF5`), `text.secondaryAccessible` (`#6F8A9D`, 4.9:1 on panel;
  the family `textMuted` `#587487` is 3.6:1), `state.danger` (fills `#E4425D`, text
  `#FF8FA0`), `state.ok`, `state.warning`, `pad.1…pad.16`, `scale.amount` (gradient
  stops), `scale.steel` (`#9CC3DA`), `lamp.hot/over`.
- Tactile material tokens (`control.body`, `control.bezel`, `control.edge`,
  `control.shadow`, `control.pointer`, `cap.metal`, `slot.recess`, `raised.surface`,
  `pad.rubber`) are listed with values on `08-tokens-components.png`. They are drawn with
  JUCE paths and gradients, not bitmaps, so states, focus and automation repaint at any
  scale on the message thread.
- Knob: vertical drag, Shift = fine, double-click = default, wheel = one step; host
  automation moves it live; gesture begin/end around each drag.
- Knob hierarchy, by musical priority (proposal ranges; smaller at 1040×700 if needed):
  - primary 48–56 px with a tick scale (Sound Editor: Tune, Decay, Drive, Cutoff, Level);
  - secondary 32–38 px;
  - Mixer and Inspector 32–40 px;
  - inline 20 px.
  Priority is shown by size and the tick scale, not by extra shading.
- Faders: neutral recessed track with a % scale and a raised metal cap. Shift = fine,
  double-click = default, value field where the existing binding offers text entry. Colour
  is reserved for the meter beside the fader (measured audio).
- Hit targets: ≥ 32 × 32 px for buttons, toggles, dropdowns and segmented controls; knob
  hit area = knob + label + value block; fader hit area = whole strip column; Mixer M/S
  ≥ 32 × 32. Exceptions: step cells (≥ 20 px high; keyboard: arrows, Enter toggles, +/−
  velocity while the grid has focus) and pad M/S (24 × 20 visual, 28 × 26 hit; the same
  actions are in List, Mixer, Inspector and as M/S keys on the focused pad).

## 6. Telemetry, meters and overload (Gate 2 feasibility)

Verified in PR #125 at `c7c29bb`:
- `Processor::takeHitMask()` returns a bitmask with no velocity.
- `takePadMeter(pad)` (post-fader pad peak) and `takeMainMeter(ch)` (Main after FX returns)
  read with `exchange(0)`, so a second reader would see zero.
- There is no limiter, clipper or clip detector on any output. The only saturation is the
  Kick drive `tanh`, which is a sound feature.
- Out 2–9 have no meters of their own.

**Coordinator.** One message-thread object (~30 Hz timer) is the only caller of the `take*`
functions.
- Each frame it drains the hit mask, the velocities and the peaks once, then publishes one
  immutable snapshot (16 pads + Main L/R). Pad Rack, Mixer, Inspector and Sound Editor read
  that snapshot.
- Momentary meter values decay in the coordinator (falloff ≈ 20 dB/s). Peak hold lasts
  1.5 s. OVER latches stay until Reset peaks.
- Changing the layout or opening a second view of the same data does not touch the
  accumulators.

**Velocity telemetry** (proposal, Architecture Guard in Gate 2; audio must stay
bit-identical). Contract: the snapshot holds, for each pad, the **maximum velocity
triggered since the previous UI frame**. It is an aggregate: it does not show every
individual hit.
- Audio thread, on each hit of pad p (host MIDI, internal pattern or song, each ratchet
  repeat, audition; muted pads excluded as today):
  `vel[p].fetch_max(v)` (a compare-exchange loop on `std::atomic<uint8_t>`, relaxed),
  then the existing `hitMask.fetch_or(bit, acq_rel)`.
- Coordinator: `mask = hitMask.exchange(0, acq_rel)`, then `v = vel[p].exchange(0)` for
  each set bit.
- A bit with `v == 0` (the velocity was taken by the previous frame's exchange in a race)
  shows a fixed-brightness flash. A velocity without a bit is discarded at the next frame.
  No hit is lost or shown twice, and the worst case is one frame late.
- If per-hit flashing is needed later: a bounded single-producer/single-consumer ring of
  (pad, velocity) events, dropping on overflow with a counter.
- Tests:
  - two UI consumers see identical data;
  - a playing pattern while the layout changes;
  - host MIDI, Internal Pattern/Song with ratchets, audition, Mute/Solo, project restore;
  - no allocation, lock or I/O in `processBlock` (existing probe).

**What each indicator means**
| Indicator | Measured where | Label and meaning |
|---|---|---|
| Knob arc, fader position | parameter value | the setting, not loudness or overload |
| Pad glow / LED | hit velocity (after telemetry) | how hard the pad was triggered |
| Pad peak bar, strip meter | pad output after its fader | measured audio of that pad |
| Strip / pad **OVER** | pad post-fader peak ≥ 0 dBFS | internal float headroom used; latched; not clipping |
| Main meter | Main after FX returns, before the host | measured plugin output, L and R |
| Main **−1 dB** | Main peak ≥ −1 dBFS | near threshold; not latched |
| Main **OVER (≥ 0 dBFS, pre-host)** | Main sample ≥ 1.0 | full scale reached: a risk for a fixed-point stage or export, not proof of clipping; the DAW master bus is not measured; latched |
| CLIP | — | **not shown**: no clipping event exists in the product |
| Out 2–9 | not metered today | a strip routed to Out n shows its pad meter. Per-bus meters need read-only taps on the bus buffers (bounded, no allocation), proposed for Gate 2 only if wanted |

No compressor or gain-reduction display: the product has no compressor.

## 7. Layout persistence (proposal; needs Architecture Guard review in Gate 2)

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

## 8. Feasibility and compatibility checklist

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
| Realtime | One telemetry coordinator reads hit/meter atomics once per UI frame (§6); no allocation, locks or I/O added to the audio callback. Velocity telemetry = 16 relaxed atomic stores per block at most. |
| Tactile controls | JUCE `Path`/`ColourGradient` drawing, cached per size/state; no bitmaps; repaint only on value or state change. |
| Overload indication | OVER for pad post-fader and Main (existing taps), honest labels; no CLIP (no source). Out 2–9 meters need new read-only taps (Gate 2, optional). |
| Minimum size | 1040×700 works (one editor, rails, Mixer collapsed). Host resize limited to it. |
| DPI | Layout in logical px; JUCE scale factor from the host. 125/150/200 % evidence is a Gate 2/3 deliverable. |
| Keyboard | No global key capture; unhandled keys pass to the host. |
| Font | Renders use Inter; JUCE uses the platform font (Segoe UI on Windows). Metrics differ slightly; layout keeps slack. |
| Family tokens | Resolved by the tech lead: opt-in shared semantic roles in `libs/vox-ui`; family surfaces unchanged (§5). |
| Generator genre fidelity | Functional work in issue #128; the UI only shows the real style setting and target. |

## 9. Limits of this proposal

- Renders are HTML/Chromium at 2× of the logical size, not JUCE output; no host was
  used. Values shown are factory defaults or illustrative pattern data.
- Interaction details (keys, gestures) are proposals for Gate 2 and will be tested
  there; nothing here is verified in a DAW.
