# VOX MUSIC · Instrument Workbench

One React/Vite workbench for three **separate modes**: VOX Drums, VOX FX and the Design System Console. This is an interactive browser reference, not software linked into the release plugin. No VST3 identity, native `ParameterID`, ABI, DSP, other branch, or build target is changed.

**Provenance:** the first browser build preceded the ZIP upload. Both original React donors are now verified at a pinned revision. This build selectively incorporates A's unit entry/contrast/binding tools and B's procedural samples, pattern transformations and local delay controls. See the [donor delta audit](docs/DONOR_DELTA_AUDIT.md) for exact reuse and intentional gaps. The [proposed VOX MUSIC 1.0 and DS-00 audit](../../docs/ui/vox-music/README.md) remain separate from native implementation and acceptance.

## Run

Node 22.17+ and npm, from the repository checkout (Console → Binding bundles the checked-in DS-00 CSV on demand):

```sh
cd apps/vox-music-prototype
npm ci
npm run dev
```

Open the Vite URL (default `http://localhost:5173/`) or select **VOX MUSIC — Instrument Workbench** in Arena's live preview. Links `#drums`, `#fx`, `#system` open a mode directly. All browser resources are local to the Vite server. The preview binds `0.0.0.0`, permits `.e2b.app`, and has no second service or browser-side localhost calls.

- **Drums:** `Play preview` runs the local clock; click a pad's rubber face to audition and its `SEL` strip to edit silently. Pattern/Song and the kit are in the top bar. Use **Panels** to show tools, drag a panel grip to the left/right dock (or click the grip to move it), and drag or use arrows on a splitter. In **Workspace**, load a layout or save/reset it. **Mixing → Expanded** exposes all 16 mixer strips via horizontal scrolling. Collapse or dismiss dock content to reclaim working room. **Step Sequencer → Pattern tools** edits the selected target in one undoable operation. **Sample Editor** offers 24 procedural demo tones, local import, trim, Reverse, Normalize peak and a 600 ms loop per hit for the chosen A/B player. **Expanded Mixer → Delay settings** sets the division of local tempo and feedback. At 1040×700 the default Mixer compacts to the collapsed strip; a manual selection takes precedence.
- **FX:** Perform, Design, Timeline, Motion, Process, Trigger, Presets, Advanced. **Preview event** and **Play visual pattern** are local visual timers; FX DSP, MIDI I/O and WAV/DAW transfer are not connected. Generation edits typed Program data. Presets support local save and validated JSON import/export.
- **Design System:** eight documentation/showcase sections. Switch Drums, FX or Synth accents without changing either instrument's data. Tokens calculates contrast for the selected accent; Binding searches/filters the static DS-00 audit and exports its full CSV. Native bindings and acceptance are documented here, not inside musical instruments.

Tab/Shift+Tab traverse controls. Sliders and knobs accept arrows, Shift for fine changes, Home/End, Delete reset and double-click reset. Their numeric fields accept plain-domain values (`85%` Level is `0.85` in the field), or compatible unit suffixes for explicit bindings (`2.5 kHz`, `105 ms`). In the Step Grid, arrows move without toggling, Space toggles, `I` inspects, Shift-based range selection and Ctrl/Cmd+click select without audition. Right-click/Alt-click inspect a step without toggling. Drag an existing parameter-lane hit for direct edits. Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z undo/redo in the active instrument (text fields keep native editing shortcuts).

## Verify

```sh
npm run typecheck
npm test
npm run build
npx playwright install chromium    # once on an ordinary development machine
npm run test:e2e                    # Playwright starts a local one-shot Vite test server
```

For restricted **Linux x64** environments with only npm registry access: `VOX_BUNDLED_CHROMIUM=1 npm run test:e2e` uses the npm test browser and its bundled NSS libraries. Browser binaries are test-only. Set `VOX_BASE_URL` to test an already running preview. Functional checks run without visual captures by default. Enable `VOX_VISUAL_CAPTURE=1` for captures/traces in `test-results/` only upon explicit owner authorization. No generated binaries or sample datasets are required.

## Source boundaries

```text
main.tsx / app.css                        shell, route, accent
 ├─ design-system-console                 overview/tokens/showcase/registry/requirements
 ├─ drums-prototype                       musical model, dock layout, Web Audio and UI
 └─ fx-prototype                          typed Program, visual event clock and UI
        └─ design-system + shared         reusable components / tokens / history utilities
shared-test-utils + e2e                  fixtures and browser evidence, never runtime imports
```

The shared library receives values and callbacks only. It has no imports from either instrument or their engines.

- **Drums repeat visits:** only the current Workspace layout and named layouts persist. Kit, pad, Pattern and Song edits, imported samples and Library snapshots are session-only and reset on reload. A layout save is not a project save or part of musical undo.
- **Drums MIDI and audio:** Step and Song download first-pass MIDI files at the local tempo in fixed **4/4**. They do not sync to a host or drag into a DAW. Main and Out 2–9 are measured Web Audio buses mixed into one browser playback device, not nine DAW outputs.
- **FX:** User Programs and favorites persist locally; A/B and unsaved work are session-only.

- [Owner handoff and requirement matrices](docs/HANDOFF.md)
- [JUCE_PORTING_MAP.md](docs/JUCE_PORTING_MAP.md)
- [Verification and manual protocol](docs/VERIFICATION.md)
- [GitHub Pages publishing and owner setup](docs/GITHUB_PAGES.md)
