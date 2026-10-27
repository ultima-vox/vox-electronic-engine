# Acid Synth — UB-oriented refactor and contract audit (2026-10-27)

## Scope and authority

Scope: PR #59 `fix/acid-contract-ui-integration`. Follow the repository's `agents/AGENT_CONTRACT.md` and existing instrument, MIDI, rack and state contracts. This pass edits **Acid sound code, its own tests and this report only**; no `libs/rack/**`, MIDI ABI or `main` changes. The plugin-wide Rack and the Acid module have **not** been accepted for Production.

No separately installed "UB MODE" skill was available in this session. The pass follows the requested undefined-behavior/compatibility-first method: establish an executable failure before refactoring, fix at the owning layer, preserve wire formats, add regression assertions, inspect Windows CI results, and decline merge without all required evidence.

## Baseline

At baseline Acid branch `0eb98e972843681a4e992aecc93b0ca38ef3beb5`, Windows workflow [40495551710](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/40495551710) **compiled**; `acid_tests`, `editor_smoke_tests`, `part_audio_isolation_tests` passed and full CTest was **29/30**. `bass_render_diagnostics/save_restore_rerender` failed. Green Acid tests do not imply a green overall gate. Earlier Acid UI APVTS attachment deduplication, note-offset overflow protection, and legacy v1 state pinning were already in that baseline and are not reimplemented here.

## Findings and remediations

| Severity | Finding | Correction / test |
|---|---|---|
| P1 — deterministic DSP sync | The musical PPQ accumulator advanced inside `AcidEngine::renderStereoSample` only when the amplitude envelope was active. A MIDI NoteOn at a nonzero sample offset after silence received stale PPQ phase for STEP and synced LFOs. | [`904c999`](https://github.com/ultima-vox/vox-electronic-engine/commit/904c9992f7a6e0f85bb60cafcf5a04f220948034) compares a delayed MIDI onset to a note starting immediately at the equivalent musical position; [`3eb78d0`](https://github.com/ultima-vox/vox-electronic-engine/commit/3eb78d0c9b7c8d8215377629e1e14e8e79c536b7) advances PPQ **once per sample** in `process`, regardless of voice gate state. |
| P1 — undefined behavior risk | A *finite* PPQ such as `DBL_MAX` can overflow after division by a short LFO period or multiplication by STEP subdivision. The old path could evaluate `fmod(inf, 16)` and cast `NaN` to an integer step-array index. | `3eb78d0` bounds derived phases through finite-aware modulo reduction; regression renders +/- `DBL_MAX` PPQ with synced LFO and STEP enabled, requiring finite bounded audio. |
| P1 — state-schema drift | v1/v3/v4/v5/v6/v7/v8 sound-state loading repeated several manually maintained suffix reset blocks and separate parsing loops. A future parameter append could silently fail to disable previously absent DSP sources. | [`3b53c69`](https://github.com/ultima-vox/vox-electronic-engine/commit/3b53c69481dc62a833220876be959df9c43e23ec) uses one version-to-prefix mapping, factory defaults for absent suffix fields, and one transactional validated float reader. Legacy v1 still overrides only IDs 0–8 and retains pre-v3 extensions 9–35. |
| P2 — silent future defaults | Fixed-size `std::array<float,parameterCount>` initialization silently zero-fills a missing new default; new parameter count may not be updated when the enum changes. | [`923212c3`](https://github.com/ultima-vox/vox-electronic-engine/commit/923212c3c7be69ac6dbb98f74e415ea5fa423d42) deduces factory-array length and adds two compile-time assertions for parameter/default/enum parity. |

## Compatibility / realtime invariants

- Stable 77 Acid semantic IDs, historical Rack macro assignments and v8 sound-state wire format are preserved. No host IDs renumbered.
- No allocation, mutex, file I/O or UI interaction was introduced in the audio sample loop.
- State parsing remains transactional: an unsupported version, undersized blob or nonfinite float returns false **without applying a partial patch**.
- Missing v3-and-later feature fields retain their factory-off values when older projects load into already-used synths. Legacy v1 semantics are explicitly pinned by `tests/acid/AcidTests.cpp`.
- Extreme-PPQ modulo fallback is taken only if intermediate phase division/multiplication is not finite; ordinary musical positions keep the normal path.

## Acceptance status / outstanding risks

- New Windows workflows for test pin, audio-clock correction and state-schema refactor were dispatched. **Their final results must be checked before review approval.** The baseline 29/30 result is not presented as new-HEAD success.
- The separate Bass `save_restore_rerender` mismatch remains unsolved and must not be hidden by weakening tests or changing Rack in this branch.
- Real DAW audition, automation, preset recall, PPQ seek/loop across diverse buffer sizes, and visual reference comparison at 1024×768 remain required.
- Free-running LFO phase behavior during fully silent periods and boundary handling of out-of-block MIDI events deserve a later dedicated contract review; no undocumented behavioral change was introduced for these cases here.

**Decision:** keep PR #59 Draft, do not merge or classify Rack/Acid as Production-ready.
