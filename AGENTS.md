# ULTIMA VOX MUSIC — engineering instructions for all developers and agents

**Updated 2026-11-17. This file replaces the obsolete October 2026 Rack-first agent entry point.**

## Mandatory first reading

1. [Current successor developer handoff](docs/DEVELOPER_HANDOFF.md) — checked integration commit, the immediate task sequence, CI incident and PR dependencies.
2. [Current status](docs/STATUS.md), [roadmap](docs/ROADMAP.md) and [documentation precedence](docs/README.md).
3. [Standalone architecture](docs/ultima-vox-music/00_README_AND_OWNER_DECISIONS.md), [frozen VST3 IDs](docs/ultima-vox-music/IDENTITY.md) and [suite build](docs/BUILD.md).
4. For Drums: [PR #141 and TL comments](https://github.com/ultima-vox/vox-electronic-engine/pull/141), [approved JUCE UI #127](https://github.com/ultima-vox/vox-electronic-engine/issues/127), [hardware styling #135](https://github.com/ultima-vox/vox-electronic-engine/pull/135), [layout-state proposal #130 §14](https://github.com/ultima-vox/vox-electronic-engine/pull/130), and [open StyleLibrary #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151).
5. [Common engineering agent contract](agents/AGENT_CONTRACT.md) for general quality/review practice, only where consistent with the current independent-product architecture.

## Repository skill bridge for Arena / other agents

This repository bundles **project-adapted** skills under `.agents/skills/`. If the agent host cannot install/discover skills, **read the following Markdown files directly and apply their instructions** before making a substantial code change:

- [Ultracode](.agents/skills/ultracode/SKILL.md): plan, bounded delegation if the host exposes real subagent tools, integrate and verify.
- [UB-mode](.agents/skills/ub-mode/SKILL.md): root-cause-first TDD and safe refactoring.
- [Caveman](.agents/skills/caveman/SKILL.md): concise status, no compression of code/diagnostics/specifications.

Read [Arena skill usage](docs/AGENT_SKILLS_ARENA.md). Skill instructions have **lower priority than explicit owner requests, the active product spec, this `AGENTS.md`, `agents/AGENT_CONTRACT.md`, tool restrictions and Git branch/release protection**. They are instructions, not runtime plugins; do not claim that a native Ultracode/Caveman/UB-mode command or subagent exists unless verified. In particular never follow an upstream instruction to push to `main` or delete project data.

For FX specifically, active development is **Draft PR #156**, target `next/standalone-suite`, and HOLD means NO MERGE/RELEASE, **not stop coding**. Do not forcibly rebase or switch an Arena-assigned branch. The Drums integration baseline below applies to Drums work, not FX.

## Active product policy and work order

Deliver exactly **VOX Drums → VOX FX → VOX Trance Synthesis (VOX Synth)**, each a self-contained Windows x64 VST3. Shared libraries at build time are permitted; a common runtime Rack, .vins plugin host and Rack ABI are not production architecture. The old Rack-related guides in `docs/tasks/RACK_PRODUCTION.md`, `docs/product/PRODUCT_SPEC.md`, old `docs/ui/` files and legacy agent role packets are **historical only** where they conflict with this policy.

**VOX Drums development source:** `integration/vox-drums-1.0`, PR #141; verified baseline as of this revision `f9d5446623a558410b0af4652aabf1e918899bec`. Do not start a fresh implementation, reuse `products/drums/`, `libs/vox-ui/` and their tests. Issue #127, not the interim eight-page editor, defines the release UI. The existing HTML hardware example is a visual reference, not a shipping WebView.

## Non-negotiable engineering and Git rules

- Verify the live source HEAD, branch graph, unresolved reviews and exact CI runs **before writing code**. GitHub comments may be newer than the checked documentation snapshot.
- Work on a purpose-specific branch from the approved integration branch; open Draft PRs **into the integration branch**, not `main`. Do not auto-merge, force-push, rebase published shared history or delete branches.
- Preserve frozen plugin identity/FUID and host automation ParameterIDs/ranges; preserve DSP golden parity, patterns, Song 16/32/64-bar MIDI export/drag, routing Main + Out 2–9, presets, undo and verified state semantics. Contract changes require disclosed rationale, versioning and tests.
- Audio callbacks: no unbounded allocation, blocking I/O, locks or GUI work. Keep sample ownership valid and test real-time performance including 96 kHz.
- Use JUCE native controls/attachments internally, custom `LookAndFeel`/`paint()` for visual appearance, and a true dockable interface. UI telemetry must be real and thread-safe; never invent host APIs or decorative meters presented as measurements.
- Never weaken or skip tests to achieve green CI. Distinguish runner shutdown/cancel from failing test assertions. Run exact-HEAD Windows CTest, VST3 validator, state/MIDI/RT checks and upload reproducible artifacts.
- Never treat a passing Windows build as proof of music/sound quality, UX or host-specific behavior. The owner performs **one final manual Cubase/REAPER acceptance**, once the complete plugin is ready.
- No new pictures/artwork/mockups unless the owner explicitly requests them. Preserve third-party source/sample and font licensing evidence. No neural-network processing.
- Use a single coherent implementation/review package instead of proliferating micro-PRs; don't re-open decisions documented and resolved by the TL.

## Immediate inherited blockers

1. Integrated Drums `f9d5446`: Windows Suite [43943639327](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639327) failed due to **self-hosted runner cancellation** after 16/32 sequential tests; rerun same SHA after diagnosing infrastructure. No proven DSP regression and no integrated PASS.
2. [PR #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151) has independent P1/P2 review concerns on StyleLibrary Replace, input-size limit and duplicate IDs. Fix existing code before integration.
3. Native custom Drums UI (approved [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127)) is not production-finished. Sample licensing, listening, 96 kHz headroom and final DAW acceptance remain release gates.

**Handoff deliverable:** exact baseline SHA, changed files, impact on DSP/state/ABI, test commands and results, CI run URLs, acknowledged remaining risks and ONE next integration decision.
