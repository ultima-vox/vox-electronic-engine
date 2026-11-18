# Documentation index and authority — standalone ULTIMA VOX MUSIC

**Updated 2026-11-17.** Earlier Rack-first documents (October 2026) remain in version history for provenance. They are **not** the current product plan.

## Precedence if documents disagree

1. Latest explicit owner decision and approved GitHub issue/contract.
2. [Developer handoff](DEVELOPER_HANDOFF.md) and [engineering instructions](../AGENTS.md) for the **current branch and immediate tasks** (both are dated snapshots; recheck the repository).
3. [Independent-product architecture](ultima-vox-music/00_README_AND_OWNER_DECISIONS.md), [plugin identities](ultima-vox-music/IDENTITY.md), [Drums spec](ultima-vox-music/03_VOX_DRUMS_SPEC.md) and implementation-specific contracts.
4. **Drums UI:** [issue #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127) controls behavior; [PR #135](https://github.com/ultima-vox/vox-electronic-engine/pull/135) controls physical-instrument styling only; [PR #130 §14](https://github.com/ultima-vox/vox-electronic-engine/pull/130) supersedes pre-release compatibility-only UI-state provisions.
5. The actual code, tests and **exact-HEAD** Windows CI evidence establish what is implemented and verified. Specifications describe targets, not evidence of completion.
6. Older `product/PRODUCT_SPEC.md`, `ui/`, Rack issue/task documents and `agents/*` assignments are historical where they conflict with the standalone direction.

## Essential working documents

| Resource | Purpose |
| --- | --- |
| [Developer handoff](DEVELOPER_HANDOFF.md) | Starting branch/commit, previous PR history, open problems, new developer execution order |
| [Status](STATUS.md) | Verified snapshot of actual development and CI |
| [Roadmap](ROADMAP.md) | Drums → FX → Synth release sequence and gates |
| [Build](BUILD.md) / [suite pins](ultima-vox-music/BUILD.md) | Windows standalone build, CI, artifact and toolchain requirements |
| [Current identity freeze](ultima-vox-music/IDENTITY.md) | Frozen plugin FUID/CIDs and naming |
| [Standalone Drums target](ultima-vox-music/03_VOX_DRUMS_SPEC.md) | 16-pad instrument sound, sampling, Song/MIDI, output and content target |
| [Bar timing](ultima-vox-music/drums/BAR_TIMING.md) | Implemented cell scheduling model and DSEQ/DSNG |
| [Styles](ultima-vox-music/drums/STYLE_ENGINE.md) and [factory](ultima-vox-music/drums/FACTORY_CONTENT.md) | Style codecs, generator and content |
| [DAW checklist](ultima-vox-music/drums/DAW_ACCEPTANCE_CHECKLIST.md) | Single final manual DAW acceptance |
| [Legacy archive index / PR #118](https://github.com/ultima-vox/vox-electronic-engine/pull/118) | Historical donor and `archive/*` preservation refs |

## Current review and decisions

- [Drums integration PR #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141): all TL decisions, integrated SHA, CI and handoff comments.
- [Drums UI/UX issue #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127): approved release interface behavior.
- [User style library PR #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151): separate Draft, do not assume code has been merged.
- [Legacy archives](https://github.com/ultima-vox/vox-electronic-engine/pull/118): relevant for reusing Bass/Acid/FX donors only after independent source audit.

## Historical documents

The October `product/PRODUCT_SPEC.md`, Rack-first `tasks/RACK_PRODUCTION.md`, `architecture/CONTRACTS.md`, `ui/DESIGN_SYSTEM.md`, `ui/WORKFLOWS.md`, and older engineering agents' reports describe the previous code base. Preserve them for traceability, but **do not revive the Rack production objective**. Historical details that remain useful (RT safety, host contracts, provenance) must be checked against the active standalone implementation before reuse.
