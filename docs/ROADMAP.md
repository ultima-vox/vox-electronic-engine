# Execution roadmap — ULTIMA VOX MUSIC standalone VST3

**Updated 2026-11-17.** Supersedes the October 2026 Rack-first development order. Three releases, **in order: VOX Drums → VOX FX → VOX Trance Synthesis (VOX Synth)**. No single runtime Rack, nested .vins host or combined Rack release.

## 1. VOX Drums — finish the existing code, no rewrite

| Order | Work | Required exit evidence |
| --- | --- | --- |
| D0 — now | Stabilize integrated Drums `integration/vox-drums-1.0@f9d544662` and restore Windows CI after the self-hosted runner cancellation | Exact integrated SHA, runner diagnostics, successful Windows build, serial CTest + gain tests, Steinberg validator and artifact hashes; previous [suite 43943639327](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639327) was canceled after 16/32 serial tests |
| D1 | Complete existing [StyleLibrary #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151), not recreate it | Fix P1 replacement file/delete atomicity and P2 direct-import size and duplicate UUID ambiguities; tests and exact-head Windows CI, independent review, integrate into `integration/vox-drums-1.0` |
| D2 | Replace interim eight-page editor with approved **native JUCE dockable workspace** | [Functional UI authority #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127), visual-only [#135](https://github.com/ultima-vox/vox-electronic-engine/pull/135), pre-release state policy [#130 §14](https://github.com/ultima-vox/vox-electronic-engine/pull/130). Real UI screenshots, layout and control/automation tests, no WebView |
| D3 | Complete musical/factory content, program and style coverage | Deterministic generator/style tests; kit/pattern/song presets, source and license manifest per redistributed sample; owner listening **before release**, not before development integration |
| D4 | Audit full Drums release-candidate code path | DSP/golden parity, RT safety/96k callback timing, 9 buses, state/undo, sample lifetime, host automation, Song 16/32/64-bar and MIDI drag/export; no known P0/P1 |
| D5 | Single integrated Windows qualification then one owner DAW acceptance | CI on **final integrated SHA**, validator, manifest/hashes, native UI evidence; one manual Cubase 12–15 and REAPER acceptance at the end. No merge to `main` until production approval |

D0 is an **infrastructure issue**, not a confirmed DSP test failure. Do not change audio code or test thresholds merely to turn a canceled job green. Actual sound is still a listening gate even when CI is green.

## 2. VOX FX — next independent instrument

Use [FX specification](ultima-vox-music/04_VOX_FX_SPEC.md) as product target, [frozen identity](ultima-vox-music/IDENTITY.md) and audited donor history [#110](https://github.com/ultima-vox/vox-electronic-engine/pull/110) / `archive/fx-00-2026-11-09` as reference only. Implement a standalone JUCE VST3 MIDI-triggered FX generator, not a Rack slot and not an incoming-audio insert effect. Reuse shared JUCE controls and safe DSP primitives from Drums where applicable; independently benchmark, test and validate FX before its own release.

## 3. VOX Trance Synthesis (VOX Synth) — last

Use [Synthesis specification](ultima-vox-music/02_VOX_TRANCE_SYNTHESIS_SPEC.md) and unique frozen identity. Audit preserved Bass and Acid donor commits (`archive/bass-recovered-2026-11-09`, `archive/acid-recovered-2026-11-09`, `archive/acid-303-rack-2026-11-09`), then transfer only independently reviewed DSP, controls and tests to the standalone synth. Include NoteOff/release and timing parity, voice resources, current parameter/state contracts, generative MIDI and dedicated JUCE UI. No reintroduction of `slotNN` Rack parameter pool.

## Cross-cutting engineering and release policy

- C++20/JUCE, Windows x64 VST3; use `.github/workflows/suite.yml` and available Windows self-hosted runners; no Linux-only CI qualification for production.
- Each plugin keeps its own processor/editor, identity, automation IDs, bounded serialized state, DSP resource/RT tests, plugin validator and reproducible binaries.
- Presets, external assets and licenses are release blockers if unverified; do not make ungrounded performance or audio-quality claims.
- Manual host acceptance is **one final stage per completed plugin**, not one manual round per PR.
- Stage verified features in each plugin's development integration branch and retain source SHA/provenance. Avoid micro-PR ladders, unreviewed main merges and branch deletion while active PRs still reference them.
- The historical October 2026 Rack-first roadmap remains in Git history and does not apply to the independent suite.
