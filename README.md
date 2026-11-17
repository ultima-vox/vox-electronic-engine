# ULTIMA VOX MUSIC — standalone Windows VST3 instruments

**Active product direction (2026-11-17):** exactly three independent C++20 / JUCE Windows x64 VST3 instruments, completed in this order:

1. **VOX Drums** — finish the existing 16-pad drum machine and its approved custom JUCE editor.
2. **VOX FX** — standalone MIDI-triggered FX sound generator.
3. **VOX Trance Synthesis** (also called VOX Synth) — standalone synth including Bass/Acid and approved synthesis capabilities.

**There is no shared Rack host in the shipping architecture.** The historical Vox Electronic Engine Rack code remains in this repository as a donor/archive, not as the default product or the active development task. Existing DSP, tests and `libs/vox-ui` are reused rather than rewritten.

## New developer: start here

- **[Current developer handoff](docs/DEVELOPER_HANDOFF.md)** — authoritative dated baseline, scope, integration branch, PRs, CI incident and next tasks.
- **[Agent instructions](AGENTS.md)** — current task precedence, safety rules and workflow.
- **[Live project status](docs/STATUS.md)** and **[roadmap](docs/ROADMAP.md)**.
- **[Documentation authority/index](docs/README.md)**.
- **[Standalone build and validation](docs/BUILD.md)** and [pinned suite build](docs/ultima-vox-music/BUILD.md).
- **[Frozen plugin identities](docs/ultima-vox-music/IDENTITY.md)**.
- [VOX Drums specification](docs/ultima-vox-music/03_VOX_DRUMS_SPEC.md) and [approved JUCE UI contract #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127).
- **[Final DAW acceptance checklist](docs/ultima-vox-music/drums/DAW_ACCEPTANCE_CHECKLIST.md)** — release gate for the completed exact-head Windows candidate; one final owner pass only.

## Current VOX Drums source of truth

The shared development branch is **`integration/vox-drums-1.0`**, GitHub [PR #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141). The checked baseline on 2026-11-17 is `f9d5446623a558410b0af4652aabf1e918899bec`. This includes the major generator/style/factory/gain chain and MIDI drag integration; it is **not** production-accepted.

The integrated Windows Suite on that SHA was **interrupted by the self-hosted runner shutdown** after 16/32 serial CTests had passed; the remaining tests and VST3 validation did not finish. [CI incident and required rerun](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6929407023). The separate [StyleLibrary PR #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151) is not yet integrated and has outstanding review findings.

Work in a new branch from the integration branch, issue Draft PRs targeting it, and use GitHub Actions on Windows self-hosted runners. **Do not merge to `main`**, delete branches or restart the Drums implementation. The manual Cubase/REAPER acceptance is a **single final stage** after the product and automated qualification are complete.

## Repository layout (active and historical)

| Path | Role |
| --- | --- |
| `products/drums/` | Active standalone Drums processor, DSP integration, sequencer, MIDI, state and interim editor |
| `products/fx/`, `products/trance-synthesis/` | Other standalone VST3 product source; presence is not feature or release acceptance |
| `libs/vox-*/` | Shared DSP, UI, state and testing infrastructure |
| `tests/suite/` | Standalone regressions, golden renders and performance checks |
| `.github/workflows/suite.yml` | Windows standalone-suite CI and validator |
| `apps/vst3/`, `libs/rack/`, `libs/bass/`, `libs/acid/` | Historical Rack code and future DSP donors; not the Drums release host |
| `docs/ultima-vox-music/` | Standalone specifications and implementation contracts |
| `archive/` | Historical design and source records |

Earlier Rack-first product specifications and October status documents are preserved for historical review in Git, but **do not supersede the active standalone handoff**. Automated CI cannot prove audible sound quality, final UI appearance or real DAW drag/routing behavior.
