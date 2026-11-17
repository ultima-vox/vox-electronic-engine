# Developer handoff — ULTIMA VOX MUSIC standalone VST3

**Verified snapshot:** 2026-11-17. This is the active engineering entry point for a successor to the prior Drums developer. Confirm GitHub HEADs, CI and comments again before editing; this document is a dated snapshot, not live status.

## Non-negotiable product direction

Deliver **exactly three independently loadable Windows x64 VST3 instruments**, in this owner-approved completion order:

1. **VOX Drums** — finish, integrate, test and prepare its own release candidate.
2. **VOX FX** — independent MIDI-triggered FX instrument; not an insert/audio-input effect and not a Rack module.
3. **VOX Trance Synthesis** (owner shorthand **VOX Synth**) — independent synthesizer incorporating Bass/Acid and other approved synth features.

No universal Rack, no shared runtime host, no .vins production dependency. Shared C++/JUCE DSP/UI libraries are allowed at build time. Preserve frozen VST3 identities in [IDENTITY](ultima-vox-music/IDENTITY.md), automation IDs and state semantics. Work is Windows x64 first with Cubase 12–15 and REAPER as final target hosts.

**Important:** root README, AGENTS, docs/STATUS and docs/ROADMAP were Rack-first as of 2026-10-26 and have been superseded by this update. Older Rack instructions in docs/product/PRODUCT_SPEC.md and agents/* are historical, not active tasks. Never start a new Drums engine or base the product on main.

## Exact Drums baseline and dependency map

- **Integration branch:** `integration/vox-drums-1.0`; **source baseline:** `f9d5446623a558410b0af4652aabf1e918899bec`.
- [Integration dashboard #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141): previous stack #143–#150 is included transitively; [MIDI drag #142](https://github.com/ultima-vox/vox-electronic-engine/pull/142) was merged at `f9d5446`. Do not re-cherry-pick or recreate them.
- [Gain staging #150](https://github.com/ultima-vox/vox-electronic-engine/pull/150): exact HEAD `5e1cf357` had successful [Windows Suite](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43805453483) and [build](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43805453493); this does not qualify the later combined integration commit.
- [New user StyleLibrary PR #151](https://github.com/ultima-vox/vox-electronic-engine/pull/151), `1757eec5`, targets `drm/factory-content`, **not** the integration HEAD. [Independent review](https://github.com/ultima-vox/vox-electronic-engine/pull/151#issuecomment-6927775544) holds it for: P1 unsafe Replace/file deletion, P2 missing public `importText()` UTF-8 input bound, P2 ambiguous duplicate UUID handling. Continue the actual patch, fix and retest as one batch, then integrate safely.
- **Open PRs are not necessarily independent features.** Most earlier Draft PR heads are ancestors of #141. Do not merge a numbered ladder one PR at a time. Preserve traceability, don't delete source branches until their dependencies and ancestry are checked.

## Current Windows CI incident — do not misdiagnose DSP

On integrated `f9d5446`, [Suite run 43943639327](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639327) ended FAILURE after **16 of the 32 serial CTests passed**, when the self-hosted runner reported `The runner has received a shutdown signal` and `The operation was canceled` at 2026-11-16 20:16:13 UTC, before the callback bench completed. This was not an observed assertion failure, and **does not establish a passing integrated build**. Eight gain tests and the VST3 validator did not complete. Integrated [build run 43943639334](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639334) was QUEUED at this snapshot. Investigate runner lifecycle/service, competing jobs and host restart logs; rerun on the exact same SHA before changing DSP. Existing self-hosted Windows runners are used through GitHub Actions; do not switch the qualification baseline to Linux.

See [suite workflow](../.github/workflows/suite.yml) and [suite build instructions](BUILD.md) for exact steps and dependency pins.

## Drums: source, contracts and priorities

**Working code:** `products/drums/` includes the existing processor, 16-pad DSP and sample/layer model, pattern/song data, generator, MIDI/export code, kit engine, mixer, presets and an **interim eight-page JUCE editor**. `libs/vox-ui/` has reusable JUCE primitives. `tests/suite/` contains deterministic/golden and product tests. Preserve existing implementation; source presence is not equivalent to verified product acceptance.

Authoritative product and UI requirements:
- [Standalone Drums specification](ultima-vox-music/03_VOX_DRUMS_SPEC.md) — musical and DSP target; reconcile historical target/implemented descriptions against current source.
- [BarTiming implemented contract](ultima-vox-music/drums/BAR_TIMING.md) — DSEQ v4, DSNG v2 and single cell timing rule supersede old descriptions.
- [Style engine](ultima-vox-music/drums/STYLE_ENGINE.md), [factory content](ultima-vox-music/drums/FACTORY_CONTENT.md) and [genre styles](ultima-vox-music/drums/GENERATOR_STYLES_V2.md).
- [GitHub issue #127, approved UI/UX v3](https://github.com/ultima-vox/vox-electronic-engine/issues/127) is the **functional UI authority**. Implement a genuine dockable native JUCE workspace (Pad Rack grid/list, sequencer/song/sound/sample editors, context inspector/generator/library/routing and ONE bottom mixer), not a reskin of eight old pages. Separate pad selection from audition.
- [PR #135 visual reference](https://github.com/ultima-vox/vox-electronic-engine/pull/135) is **visual direction only**. No WebView/HTML in shipping VST3. Reuse `libs/vox-ui`, host APVTS attachments and proper on-message-thread painting.
- [PR #130, section 14](https://github.com/ultima-vox/vox-electronic-engine/pull/130) simplifies pre-release UI-state requirements: do **not** implement obsolete DUIS-page migration or dual-write solely for unpublished development builds; adopt one bounded, versioned layout state after checking the final contract.
- [DAW acceptance checklist](ultima-vox-music/drums/DAW_ACCEPTANCE_CHECKLIST.md): manual Cubase/REAPER checks happen **once at the end**, after a complete candidate passes automated gates.

**Remaining release gates:** full native UI and genuine bindings; style/library integration; usable factory content and verified redistribution rights (Rock/Metal acoustic samples were not release-ready); bounded real-time memory, audio callback timing including 96 kHz; state/undo/restore; 9 stereo output bus behavior; one-pass 16/32/64-bar Song and MIDI drag/export parity; qualified Windows build/test/Steinberg validator and captured artifacts. Gain checks verify **sample peaks only for the tested factory matrix**, not true peak or all arbitrary user kits. CI success alone does not demonstrate sound quality or Cubase/REAPER interaction.

## Work protocol for the successor

1. Clone/fetch GitHub and create a **new feature branch off `integration/vox-drums-1.0`**. Record `git rev-parse HEAD` and all relevant PR/CI SHAs. Do not implement off `main`, `fx/base`, or an old `drm/production-candidate` branch.
2. Reconfirm exact HEAD, read this guide, #141 and its TL comments, #127, #130 §14, #135, #151 and actual source. Report only genuine gaps; do not redesign features already finished.
3. Diagnose interrupted integrated CI and establish exact-HEAD Windows evidence. Then finish/fix #151 without discarding its code, and implement the JUCE UI as one coherent product feature, not dozens of micro-PRs. Independent technical review after a meaningful package.
4. Maintain one source of truth for host parameters and pattern/song state. No DSP rewrite, plugin identity change, unsafe audio-thread allocations/locks/I/O, test weakening, speculative API or unlicensed bundled assets.
5. Open **Draft PR(s) targeting `integration/vox-drums-1.0`** with precise changed paths, affected contracts, tests, Windows run URLs, known risks and next gate. No auto-merges, force-pushes, branch deletions, or modifications of `main`. Keep branch cleanup a separate track.
6. Produce one full final automated qualification and a candidate for the owner's **single final DAW acceptance**. No intermediate manual acceptance round trips.

## Start safely

```powershell
git clone https://github.com/ultima-vox/vox-electronic-engine.git
cd vox-electronic-engine
git fetch origin
git switch -c dev/drums-finish origin/integration/vox-drums-1.0
git rev-parse HEAD
```

At the dated baseline this prints `f9d5446623a558410b0af4652aabf1e918899bec`. If it differs, inspect Git log/PR/CI and update the handoff status rather than resetting or force pushing.

**Decision trail:** [TL successor handoff comment in #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6938653928), [CI cancellation diagnosis](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6929407023), [branch cleanup manifest](https://github.com/ultima-vox/vox-electronic-engine/pull/141#issuecomment-6927545756).
