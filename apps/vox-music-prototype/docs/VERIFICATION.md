# Browser verification and owner review protocol

The initial browser implementation and the later donor delta were run separately. The intervening [DS-00 documentation pass](../../../docs/ui/vox-music/README.md) did not rerun browser tests or add captures. Only functional checks ran in the donor delta; new captures require explicit owner authorization.

Run from `apps/vox-music-prototype/`:

```sh
npm ci
npm run typecheck
npm test
npm run build
VOX_BUNDLED_CHROMIUM=1 npm run test:e2e      # restricted Linux x64
# Or npx playwright install chromium && npm run test:e2e
```

For the built app, start `npm run preview -- --port 5173 --strictPort` and run the browser suite with `VOX_BASE_URL=http://127.0.0.1:5173`. Playwright uses no hosted audio files. Each test has a fresh browser context; no previous local storage required. `VOX_VISUAL_CAPTURE` defaults off (including traces and failure screenshots).

## Donor delta run · 2026-11-18, 09:51 UTC

Node `22.22.3`, npm `10.9.8`, Vite `7.3.8`, Linux x64 Chromium from the existing npm fallback. Served build assets `index-BNCaFA3t.js` and lazy `BindingAudit-B4pal8jn.js` were compared with `dist` under the Arena preview Host header.

| Check actually run | Result |
| --- | --- |
| A/B original archives | Both lockfile installs (`--ignore-scripts`), TypeScript, builds and targeted Chromium smoke checks completed separately. [Provenance and exact comparison](DONOR_DELTA_AUDIT.md). |
| Workbench `npm ci --ignore-scripts --no-audit --no-fund`, `npm run build`, `npm run typecheck` | 100 packages, compiled successfully. No package changes or native build. |
| Workbench `npm test` | **22/22** including nine additional donor delta checks. Original vs imported B PCM also compared **24/24** byte-for-byte in Node. |
| `VOX_VISUAL_CAPTURE=0 VOX_BUNDLED_CHROMIUM=1 VOX_BASE_URL=http://127.0.0.1:5173 npm run test:e2e` | **24/24**, **28.6 s**, including the original 19 functional regressions and five new donor tests. |

New checks cover exact exported CSV bytes, contrast recalculation on theme switch, valid/invalid units and Escape, Song copy-on-edit and no-op Undo, actual Web Audio sample starts, normalized gain and loop bounds, A/B player independence and a measured delay return at local tempo. The first full run detected an ambiguous hidden-text locator because both Binding tables contained the same ID; the regression now asserts both Console tables are hidden and the active Drums main has no host ID. Final run passed with only Playwright's text/JSON result metadata and no images or traces.

## Initial browser run · 2026-11-18

Arena Linux x64, Node `v22.22.3`, npm `10.9.8`, Vite `7.3.8`, headless Chromium `149.0.7827.0`. Tested the built Vite preview (`0.0.0.0:5173`; Arena preview Host header returned HTTP 200).

| Check actually run | Result |
| --- | --- |
| `npm ci --no-audit --no-fund` | Clean install from lockfile: 100 packages. |
| `npm run typecheck` and `npm run build` | TypeScript and Vite build completed; no native build invoked. |
| `npm test` | **13/13** model/control/boundary tests passed. |
| `VOX_BUNDLED_CHROMIUM=1 VOX_BASE_URL=http://127.0.0.1:5173 npm run test:e2e` | **19/19** Playwright tests passed against the built app. |

The browser run covered all 16 per-pad measured Web Audio signals, silent selection, meter/bus isolation and Kit Level, MIDI file generation, sample decode/trim, grouped gestures/undo, Step/Song and copy-on-edit, Generator locks, real pointer dock drag/preview and resizes, saved layout restoration, Inspector pin, theme and control-state fixtures, FX work areas/curve editing and preset recall. Axe WCAG 2.0/2.1 A/AA rules found no violations on the three tested **landing surfaces**; that does not certify every state or replace assistive-technology testing.

The suite captured the four required viewport sizes and Composition, Sound Design (decoded sine test fixture), Mixing, docked Generator, split fallback and FX Timeline. Those historical captures were in ignored `test-results/`; later runs do not generate new ones by default. At 1280×720 with Compact selected, peripheral panels and the step editor scroll independently; collapsing Mixer reclaims vertical room. Initial testing caught a first-note leak through Web Audio sends whose default gain was unity; the strips now initialize closed and the final output-isolation test passes. No React donor engine was available for that initial comparison.

## Review the browser in Arena

Use actual interactions, not a screenshot-only pass. Record browser/version, logical viewport, device scale, layout name, path, what you heard/observed, and any screenshots before marking an item accepted. Owner visual approval remains pending.

| ID | Check | Procedure and expected result |
| --- | --- | --- |
| VIS-01 | Baseline / four sizes | Open fresh Beatmaking at 1040×700, 1280×720, 1440×900, 1600×1000. The header, pad and Inspector chrome, central editor, bottom Mixer and footer fit without horizontally scrolling the page. The 1040 default folds the Mixer to one status strip; manually choose Compact/Expanded. Overflow uses the nearest editor/panel scroll container. Inspect rubber corners, engraved labels, focus, readable units and amber vs neutral states. |
| VIS-02 | Layout presets / all states | Workspace → each of Beatmaking, Composition, Sound Design, Mixing. Composition provides Song + Step; Sound Design shows signal flow and Sample; Mixing exposes all channels; Generator can sit beside Step via Panels. At the narrow window a side-by-side split degrades to two selector tabs. Maximize/restore and ensure each editor becomes available again. Check Collapsed, Compact and Expanded remain below the editors. |
| DND-01 | Side dock drag / reject | Start local playback. Grab the Pad Rack grip and move over left/right: the pending side marks the actual target. Release over center to reject, then drag into the Inspector side to create a tab. Click the grip to do the equivalent from the keyboard. Playback and edited pattern stay intact. Hide, re-show, make a named save, reset, restore and reload. Move a splitter with pointer and arrow keys; ratio/edges respect minima. |
| DND-02 | Sample / curve | In Sound Design, drop a local mono/stereo WAV under 12 MB and 30 seconds into Player A; repeat using the file chooser. Choose a procedural donor tone in B and verify independent Reverse, Normalize peak, bounded Loop and Panic. Verify actual waveform, drag trims (including bounds), reverse, audition and remove. Try a non-audio file; the previous sample remains. On FX Timeline, drag/nudge a node, Shift to multiselect, add/remove, snap, change shape, zoom, undo and paste to another curve. In Motion, drop a macro on the chosen typed destination, then use Assign with keyboard only. |
| MUS-01 | Pad and Inspector | Pick a different `SEL` (or `Select` in List); nothing sounds. Pin the old pad/step/bar; change selection and layout; pinned target stays fixed. Audition a different pad and verify selection does not change. The transient flash indicates MIDI velocity; pad/mixer peak reflects real gain and the selected tap. Verify Mute/Solo and distinct timbres by listening, not by color alone. |
| MUS-02 | Steps & Song | Program a hit with pointer and Space, select a range, adjust Velocity/Probability/Ratchet/Microtiming directly or in Inspector. Test 16/32/64 resolutions and Pattern tools with an Undo. In Composition pick two linked bars; Edit Source affects both. Edit This Bar has no effect until a real change, then only that bar changes. Undo restores the link. Play the whole Song and toggle selected-bar loop. Download Pattern/Song MIDI; this is a browser file, not a DAW drag. |
| MUS-03 | Real mixer signals | Route a pad to Out 2 with sends off; Main stays quiet and Out 2 moves while auditioning. `Kit Level / All outputs` to zero silences Out 2 and Main; return it to default with double-click. Verify Pan and sends at audible level. In Expanded Mixer, choose Delay settings and confirm each division responds to local BPM, with reverb unchanged. Check the mono pad and separate L/R Main tap; do not infer verified clipping from `OVER`. Panic cancels current preview; Stop preserves audible tails. |
| KEY-01 | Keyboard and parameters | Start with Tab/Shift+Tab. Check visible focus in headers, dock layout, step grid and numeric fields. Use arrow movement in tabs, `I`/Shift selections on Step Grid, knob arrows/Home/End/Delete, exact number Enter and Escape, Shift-drag fine vs coarse, double-click actual default. Move between modes without leaking shortcuts or audition. |
| FX-01 | Honest FX behavior | Run Visual Event / Visual Pattern, switch Program A/B, select different family, edit route depth and verify base does not change. The phase and ≤8 event counts follow the UI clock. DSP/meter/MIDI/CPU are unavailable, grain cap 64 is not activity, no WAV/DAW drop claims. Save/reload a JSON Program or import an invalid version and confirm safe refusal. |
| DS-01 | Console | Compare Drums, FX and Synth tokens. Use Normal/Selected/Disabled/Automated/Modulated/Warning/Error fixtures, plus pointer hover/pressed and keyboard focus. Check calculated contrast, DS-00 search/filter/export and that neither instrument includes contract tables. |

Context to retain with captures (scheme, not a fixed canvas):

```text
header: VOX Drums / kit / workspace / reversible edits / local preview
left dock ⇄ [central editor (or editor — splitter — editor)] ⇄ right dock
bottom: one Mixer (Collapsed / Compact / Expanded), never overlaying editors
short height: fold the default Mixer first; panels/editors own scrolling
narrow center: render one editor and expose the other via fallback selector
```

**Not established by browser checks:** full A/B parity, owner visual acceptance, real-device listening quality, Windows native DPI, cross-browser Safari/Firefox or mobile testing, DAW/host MIDI/routing/automation, VST3 validator and release performance. Do not mark a native or visual owner gate passed from this document.
