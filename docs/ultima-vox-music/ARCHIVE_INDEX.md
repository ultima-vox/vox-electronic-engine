# Legacy Rack archive and standalone-suite donor index

**Recorded:** 2026-11-09 UTC. **Status:** tracking document only; archival **branches** created and verified; archival **tags / protection rules are not yet created**. This file does not certify a release or merge any implementation.

**Current product direction:** three separate Windows x64 VST3 products — VOX Trance Synthesis, VOX Drums and VOX FX — defined in [#111](https://github.com/ultima-vox/vox-electronic-engine/pull/111). The old combined Rack + .vins release direction [#77](https://github.com/ultima-vox/vox-electronic-engine/issues/77) is superseded **as a shipping target**, not erased from engineering history. The new suite is developed in stacked Draft PRs; original integration and donors remain intact.

## 1. Verified immutable source *commit identities* and archival pointers

Each `archive/*` branch below was created as an **additional pointer** to the exact inspected full commit SHA and independently read back through the GitHub ref API on 2026-11-09. No source branch was moved, rebased, removed or merged. **These are branches, not protected or immutable Git tags.** Owners should apply a GitHub ruleset preventing force pushes, updates and deletion of `archive/*`, and (if archival tags are required) create annotated signed/protected tags separately.

| Snapshot | Verified full commit | Preservation ref | Donor / status |
| --- | --- | --- | --- |
| Legacy Rack + Drums | `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1` | `archive/rack-legacy-2026-11-09` | [#78](https://github.com/ultima-vox/vox-electronic-engine/pull/78); 16-slot Rack, .vins loader, 8-pad Drum module, UI/transport/state, Windows historical CI. Not a standalone product. |
| Drums DSP optimization | `be102f87b85c0333efb7e3a6a5a56807bd87995c` | `archive/drums-dsp-perf-2026-11-09` | [#109](https://github.com/ultima-vox/vox-electronic-engine/pull/109); performance candidate and test methodology. Existing 96 kHz / 64 tail/headroom concerns remain; no release approval. |
| Acid-in-Rack integration | `34f2eb5a041584672c262af98224f8d8d4552359` | `archive/acid-303-rack-2026-11-09` | [#92](https://github.com/ultima-vox/vox-electronic-engine/pull/92); important Acid DSP / integration donor, but legacy P1 HOST-pattern MIDI export and P2 block-boundary NoteOff reviews remain unresolved for this implementation. |
| Acid restored controls/UI | `c23b88d2af70c90e38930d7106899144a116d2c5` | `archive/acid-recovered-2026-11-09` | [#69](https://github.com/ultima-vox/vox-electronic-engine/pull/69); distinct donor history, *not* automatically contained by #92. |
| Bass restored module | `a4889aa86dfa2a9d3094d319c9374ee00aab5abd` | `archive/bass-recovered-2026-11-09` | [#70](https://github.com/ultima-vox/vox-electronic-engine/pull/70); independent Bass DSP/module donor requiring new standalone parity, NoteOff/release and UI coverage. |
| FX nested-Rack architecture audit | `e2f7cef410f7e9953ce80b60451ca171d89e34ee` | `archive/fx-00-2026-11-09` | Closed [#110](https://github.com/ultima-vox/vox-electronic-engine/pull/110); analysis only. Do **not** implement the rejected Rack ABI V2.5 extension to build standalone VOX FX. |

To verify any pointer read-only:

```powershell
git fetch origin --prune
git rev-parse origin/archive/rack-legacy-2026-11-09
git rev-parse origin/archive/drums-dsp-perf-2026-11-09
git rev-parse origin/archive/acid-303-rack-2026-11-09
git rev-parse origin/archive/acid-recovered-2026-11-09
git rev-parse origin/archive/bass-recovered-2026-11-09
git rev-parse origin/archive/fx-00-2026-11-09
```

Do not move these refs. Their read-only intent is **policy**, not technically enforced until a ruleset exists. Actual annotated tags are still a separate pending action.

## 2. Donor extraction ledger — tracked, not implicitly approved

| Component and provenance | New suite evidence / next required gate |
| --- | --- |
| Shared DSP primitives, `EffectsChain`, VOX UI, allocation probe from Rack `f9c8bc6` | [#114 FOUND-02](https://github.com/ultima-vox/vox-electronic-engine/pull/114) Draft, donor SHA checks and effects golden renders. This is an independent transfer; staging/merge authorization remains separate. |
| Legacy 8-pad Drums reference from `f9c8bc6` | [#115 DRM-00](https://github.com/ultima-vox/vox-electronic-engine/pull/115) Draft: test-only reference and 540-row golden corpus. Reference intentionally excludes the later #109 optimizations. |
| New standalone Drums processor, parameters and state | [#116 DRM-01](https://github.com/ultima-vox/vox-electronic-engine/pull/116) Draft: legacy engine through private adapter, 63 original parity renders and independent host tests. Two prior P1 review findings were fixed at `1579cbe`; rely on exact-head independent review before merge. |
| Standalone Drums sample loading and ownership | [#117 DRM-02](https://github.com/ultima-vox/vox-electronic-engine/pull/117) Draft: Synth/Sample/Layer, user file browser/drop/restore and parity. Stale completion P2 fixed at `968fdbc`; provisional sample memory limits still need product contract. |
| #109 optimized Drums engine | **NOT YET adopted by default.** Its reference and optimized implementation differ from the `f9c8bc6` DRM-00 golden baseline; import only after source-level audit, complete golden/parity and Windows x64 realtime-headroom tests on the standalone product. |
| Bass `FullOnBass.*` | [#70](https://github.com/ultima-vox/vox-electronic-engine/pull/70) preserved. Future SYN-series must retain exact donor SHA, independently verify NoteOff/release, MIDI, parameter state and sonic/CPU acceptance; no automatic merge of Rack ABI code. |
| Acid engine, sequencing and controls | [#69](https://github.com/ultima-vox/vox-electronic-engine/pull/69) and [#92](https://github.com/ultima-vox/vox-electronic-engine/pull/92) preserved separately; do not copy legacy host MIDI export or early NoteOff boundary behavior as if reviewed/fixed. |
| FX-00 contract audit | Closed [#110](https://github.com/ultima-vox/vox-electronic-engine/pull/110) preserved for comparison; future VOX FX is a standalone JUCE VST3 processor, not a nested Rack module. |
| Suite foundation and identity | [#111](https://github.com/ultima-vox/vox-electronic-engine/pull/111) specification; [#112](https://github.com/ultima-vox/vox-electronic-engine/pull/112) donor audit; [#113](https://github.com/ultima-vox/vox-electronic-engine/pull/113) three VST3 shells. Keep original donor references in provenance CSV and require independent review and exact-head Windows CI for each port. |

Do not confuse **tests passing on the legacy donor** with acceptance of the new product. The product-specific final DAW release acceptance remains outstanding.

## 3. Status of PR queue

A previous housekeeping pass closed 25 obsolete PRs (18 absorbed legacy Rack stack links, four ancestor/superseded PRs and three obsolete design-only proposals); see [#78 cleanup report](https://github.com/ultima-vox/vox-electronic-engine/pull/78#issuecomment-6726777343).

**Still intentionally open:** historical donor PRs #78, #92, #69, #70, #109, and active suite PRs #111–#117 (as of 2026-11-09). Do not close any remaining donor merely because an archive ref exists. Close individually only when the new product has an independently checked extraction/traceability decision, corresponding tests, and no outstanding source needed from that branch. Closure never constitutes a merge.

## 4. Outstanding archival and release-control actions

1. Owner: configure GitHub repository rulesets to **block all update, force-push and deletion** operations for `archive/*` branches. Do not claim immutable archival until checked.
2. Create annotated **archive tags** (and protect them) if required by the original archival policy. The linked GitHub connector can create branches but does not provide a tag-writing action. Do not represent branches as signed/annotated tags.
3. Owner/reviewer: review this documentation-only PR, then merge separately if approved into `next/standalone-suite`; no automatic update to `main`.
4. Donor leads: maintain the provenance ledger as suite transfers proceed; the original #78/#69/#70/#92/#109 refs stay accessible until source audit is complete.
5. After new releases: review whether preserved legacy PRs can be closed as superseded. Never delete valuable refs or transfer unresolved P1/P2 behavior as production code.

**No merges, source modifications, resets, force-pushes or branch deletions were performed during archive-pointer creation.**
