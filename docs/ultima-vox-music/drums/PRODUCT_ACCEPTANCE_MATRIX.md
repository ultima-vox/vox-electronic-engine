# VOX Drums: developer product acceptance ledger

This is a developer traceability assessment for #127, #135 and #136, not TL sign-off. The PR remains Draft/HOLD: the product and Windows gates below are open. The starting code baseline for this package was `17b3b9dde26784bd9948a555c2cdf5397198636a`; the screenshots and tests are from the follow-up tree containing this ledger. The published PR head and its CI runs identify the exact reviewable revision.

| Area | Present in production editor / implementation | Still open |
|---|---|---|
| Workspace, pads, Step | Actual SuiteEditor hosts the workspace, 16 pad controls, sequencer, Inspector and mixer; selection, step edit and Undo have executable coverage. | Hardware-reference visual quality and interaction acceptance are not established. Pads at compact width are narrow; typography and hierarchy remain dense. |
| Generator | Visible Generator shortcut opens the tool; Generate Pattern and Generate Song produce content. The production-editor test exercises generation, inspecting/editing and Undo. | A full musician listening evaluation in Cubase is outstanding. Automated pad triggering is not proof of song playback in a host. Full typed generator-setting preset recall with its seed is not supplied. |
| Presets | Visible Presets shortcut; Kit, Pattern and Song factory entries can be loaded, and Undo Load restores project state in the production-editor test. Factory rows show listening/release status. | Catalog is examples, not a completed, auditioned release library; see counts below. A Pattern example does not load its associated kit or persist style identity. |
| Song / MIDI | Song lengths 16, 32 and 64, Generate Song, and whole-song MIDI snapshot rendering are covered. Generated MIDI is parsed and contains note events reaching the final generated 4/4 bar. | Real OS drag/save dialog and Cubase import/playback at each length remain host acceptance tasks. Four/eight-bar songs are not represented by the current Song model. |
| Sound and sample editing | Sound/Inspector and sample-related controls exist; existing tests cover portions of state and file handling. | A complete musician workflow and real licensed acoustic content, particularly Rock/Metal, remain unverified. |
| Mixer / outputs | Mixer is present with pad and output controls; processor architecture retains nine outputs. | Visual, routing and interaction acceptance in the target DAW is still required. Compact width requires scrolling. |
| Docks, layout and focus | Menu-based tool placement, resizable workspace and layout persistence exist and have automated coverage. | The reference's direct drag/drop docking preview and rejection behavior are not demonstrated by this package. Keyboard/focus, clipping and display scaling need actual host verification. |
| Visual reference #135 | The current UI has a dark chassis, highlighted controls and dial treatment; actual component snapshots are linked below. | It does not establish the requested hardware finish: the compact pad faces are narrow/flat compared with the large tactile square rubber pads, and the layered chassis, metallic controls and recessed faders need further visual work. Do not treat these captures as visual approval. |

## Production-editor captures

These PNGs are JUCE component snapshots of `DrumsProcessor::createEditorAndMakeActive()` and the real `SuiteEditor`/`DrumsWorkspace`, rendered on Linux at the listed outer pixel dimensions. They are not Windows screenshots, native-window scaling evidence, Cubase screenshots, or human visual approval. Generator and preset captures use the real editor after opening those tools; the preset capture scrolls to show the explicit Rock/Metal status.

- [1040×700](evidence/product-acceptance/production-1040x700.png)
- [1280×720](evidence/product-acceptance/production-1280x720.png)
- [1440×900](evidence/product-acceptance/production-1440x900.png)
- [1600×1000](evidence/product-acceptance/production-1600x1000.png)
- [Generator at 1040×700](evidence/product-acceptance/production-generator-1040x700.png)
- [Factory readiness at 1040×700](evidence/product-acceptance/production-presets-readiness-1040x700.png)

## Factory catalog and scope

The current UI has **11 factory Kits** (9 listening pending, 2 NOT RELEASE-READY), **23 factory Pattern entries**, **26 factory Song entries** (three original plus 23 style examples), and **0 factory Pad presets**. User files are separate and are not included in these counts. The manifest `products/drums/factory-content.csv` has 63 records: 11 kits, 26 pattern collections and 26 songs, with 53 listening-pending and 10 not-release-ready records. A manifest pattern collection is not the same unit as a UI Pattern entry.

The 23 Pattern entries each supply an example A/B pair for the selected slot; the current stored-pattern maximum is four bars. Longer 8/16/32/64-bar single patterns are not representable in this model. The 23 style Song examples are 32 bars; three original family songs are 16 bars. There is no per-style factory set for every requested 16/32/64 length, and 4/8 are outside the current Song model. Independent Generate Song supports 16/32/64; this does not create a complete auditioned preset library.

All factory examples require human listening. Rock and Metal kits are synth sketches, not release-ready acoustic samples; appropriate licensed source material and its verification remain outstanding. The UI now labels factory examples and the two readiness states rather than suggesting that these entries are approved content.

## Automated evidence and outstanding gates

`suite_drums_workspace_tests` uses a fresh processor and real production editor. It invokes the editor's button callbacks and checks generated Pattern content, an audible MIDI-triggered mapped pad, selected-step editing and Undo, Song generation/export snapshots reaching the final bar at 16/32/64, and Kit/Pattern/Song Load→Undo restoring the complete serialized state. It also checks the selected slot, kit and transport mode through the generation workflow. The generated pad audio check does not exercise host sequencer playback. These are Linux automated checks and are not a substitute for Windows, native pointer interaction or DAW acceptance.

The strict Drums real-time gate remains open on the previously reported exact-head Windows CI: p99 exceeded the existing threshold at 96 kHz / 64, 128 and 333 samples. Do not relax thresholds or open duplicate issues. Exact-head Windows build, suites, validator and RT results must be re-established for the published follow-up head. No owner DAW test should be requested until the implementation and required gates are ready.

**Ready for the one final Cubase/REAPER owner pass only when:** the remaining #127/#135 product and interaction gaps have been implemented and independently reviewed, the required #136 factory examples and licensing/listening gates have been resolved or explicitly scoped by the owner/TL, actual Windows VST3 peer and DPI/Tab evidence is complete, and the exact published candidate has passed Windows build, complete suites, validator and the existing strict RT qualification. Until then, do not request another owner build test.
