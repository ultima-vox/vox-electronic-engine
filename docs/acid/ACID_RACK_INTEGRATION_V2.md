# Acid ↔ Rack V2: independent instrument and parameter integration contract

**Status:** implementation design/acceptance packet for PR #59; **not** a claim that the module, adapter, or DAW acceptance exists. Updated 2026-10-28. Shared Rack/API ownership: #62/#64/#65; standalone delivery requirements: #63. This packet supersedes conflicting interim Acid-specific host-ID instructions in `ACID_PRODUCTION_ROADMAP.md`, but **does not** authorize independent changes to shared Rack contracts by the Acid agent.

## Product boundary — one Rack VST3, separately installed Acid

- The DAW loads one Vox Rack VST3. Acid must be installable/updateable/removable **without rebuilding/changing the Rack binary**. Deliver its own DSP and descriptor as an independent native Windows x64 module, plus Acid-specific UI definition/assets, Sound presets, Pattern presets and Generator profiles. It is not another DAW VST3 and is not an arbitrary third-party VST host.
- Prefer package-shipped *declarative* Acid workspace UI using host-owned VOX widgets, including a real, reusable sequencer widget. If this cannot reproduce the accepted 303 editor, propose a versioned opaque native-editor C interface for review. Never pass JUCE/C++ objects or STL ownership across the binary boundary.
- A small, versioned C ABI with capability/size fields and explicit host services must be approved by the Rack owner before final dynamic-module packaging. No new loader code in PR #59 without that sign-off. A DLL crash remains an in-process DAW crash risk.
- `AcidEngine`, `AcidGenerator`, parameter semantics and the accepted Acid SOUND layout remain Acid-owned; Rack global header, rail, MIDI routing, FX, ducking and general host automation remain Rack-owned.

## One semantic parameter, one base value, one host authority

As of PR #59, the engine has **77 semantic state fields** including packed `play_mode_flags`. The interim host adapter exposes the other 76 fields individually and four separate play flags = **80 independent host-facing controls**. Eight of those currently use the existing Rack macros. This is an inventory fact about the current branch, not approval of either the old Acid-specific APVTS IDs or the 64-pool limit.

1. Preserve all 80 independent control semantics. One knob/selector/toggle/step level updates exactly its intended base parameter, except two views of the *same* semantic control may intentionally mirror one value. No aliasing to squeeze controls into a smaller pool, no grouped automation of independent flags and no hidden coupling.
2. A single shared Rack host adapter owns VST3 endpoints, automation gestures, state restoration and typed conversion. Remove/retire `AcidParameterBridge`, `slotNNAcid_*` APVTS registration and duplicate macro-to-Acid base writes from the **production** integration only after equivalent tests pass. Do not delete working Acid code prematurely.
3. Keep Rack macros as independent *assignable performance* controls, not hard-wired shadow copies of the first eight Acid sound parameters. For the simplest direct-host experience, reserve one distinct fixed-pool endpoint for **each** of Acid's 80 controls; macros remain separate optional performance assignments, with explicit conflict rules.
4. The Rack owner must finalize pool capacity *before freezing the VST3 layout*. Compare **96** (16-slot total 1536, Acid margin 16) vs **128** (total 2048, Acid margin 48); 128 is the preferred long-lived candidate **only if Windows Cubase/REAPER metadata and instance-create performance are acceptable**. 64 does not satisfy the 80-direct-control design. This is an integration gate, not authorization to change constants blindly.
5. Define a checked-in explicit Acid map `{stable semantic ID, poolIndex, type, range, unit, choices, default, automatable, modulation eligibility}` as the **only** catalog; do not derive stable indices from array order. Keep existing indices stable across future instrument versions, never reuse removed indices, and reject duplicate/out-of-bound IDs or VST3 hash collisions.
6. Distinguish UI-visible parameters from DAW-host-exposed parameters in the generic architecture. The current Acid requirement is that **all 80** are independently host-automatable; general future packages may contain more internal, nonhost parameters than pool slots, with clear eligibility and failure handling rather than a fake knob.
7. The audio processor applies base automation to the named parameter only. Modulation is additive/transformative at the effective-DSP layer and **never writes another base value or host automation lane**. Presets change only the intended Sound/Pattern/Generator domain; all state updates are transactional.

### Typed control examples (requirements, not verified current provider behavior)

| Semantic control | Host type | Domain / display |
|---|---|---|
| `cutoff` | continuous | frequency with correct Hz conversion |
| `filter_mode`, `dist_mode`, `waveform` | discrete choice | exact named options; correct step count |
| `unison_voices` | discrete integer | 1–4 voices |
| `lfo1_sync`, `lfo2_sync`, `lfo3_sync` | boolean | Off/On, no analog interpolation |
| `legato`, `retrigger`, `vel_to_accent`, `slide_on_overlap` | four independent boolean endpoints | packed flags are a private backward-compatible Acid state encoding only |
| `step_01`…`step_16` | 16 distinct continuous bipolar controls | each editable, stored and independently automatable |

**Audit required:** `AcidProvider.cpp` currently describes extended controls largely as normalized floats and a static built-in provider. Replace this interim metadata with truthful typed descriptors and a reviewed parameter normalization contract; test both plug-in GUI and host presentation. Never change existing host-normalized semantics merely by changing names.

**Waveform type requires reconciliation:** the example above lists `waveform`
as a discrete choice, while the control matrix requires a waveform selector and
continuous morph knob as two views of the same parameter. Current
`renderVoiceOscillator` continuously interpolates four waveforms. Advertising
this parameter as a discrete choice would quantize and alter existing morph
automation. Preserve the actual continuous behavior until the intended host
contract is reviewed; LFO waveform selectors are distinct discrete controls.

**Identity inventory checkpoint:** `AcidControlInventory.h` enumerates the 80
distinct semantic IDs with explicit proposed pool indices and distinguishes the
four flags from packed engine state. `acid_control_inventory_tests` checks
uniqueness, coverage and engine identity. This is not the required complete
typed manifest: units, physical ranges, choice conversion, defaults, modulation
eligibility, host normalized-value stability and final capacity remain to be
specified and verified. Nothing is bound to the host pool by this checkpoint.

## Automation safety and instrument replacement

- Fixed `slotNN.pNNN` endpoint IDs must never change when adding instruments. Same-InstrumentId version upgrades may use #65's checked append-only migration if recorded pool indices, preserved semantics and values satisfy the contract. Incompatible/missing packages retain the original identity, named values, Sound/Pattern/Generator blobs and automation without a fallback.
- **A different InstrumentId is not an upgrade.** When replacing an instrument in an occupied slot, do not silently send old slot automation to unrelated new controls. Propose the smallest DAW-safe workflow (e.g. show a clear replacement confirmation and inhibit stale slot automation until an explicit rearm/rebind). The Rack owner must approve and validate this in real hosts; a warning label alone does not make reinterpretation safe.
- Since VST3 hosts store normalized automation and may cache step counts/names/units, changing `poolIndex`, value domains or choice ordering can change sound even if names remain the same. Preserve/migrate semantics explicitly; host metadata notification is not a universal compatibility guarantee.
- Verify gesture start/edit/end, switch while playing/stopped, DAW project save/reopen, empty/missing slot and installed-module update in Cubase 12–15 and REAPER.

## Realtime and lifecycle work

- The present `AcidParameterBridge::apply` scans slots and reapplies most parameters **each block**; `AcidEngine::setParameter(id)` searches the IDs linearly. Replace with an instance-local prevalidated indexed binding and change/dirty-value publication. Publish immutable or safely synchronized snapshots off the audio thread. No loader/file I/O/heap allocation/blocking lock/UI callback in `processBlock`.
- `AcidEngine::process` currently smooths all 77 parameters per sample. Classify each parameter (audio-rate continuous, control-rate continuous, discrete choice/boolean, sequencer value); smooth only where musically necessary, not every selector/flag/STEP value. Establish the audible and numerical behavior with tests; measure before changing the DSP to avoid shifting the sound accidentally.
- Maintain correct note ownership, NoteOff/CC120/123/Panic, host tempo/PPQ, tail/release, and allocation-free audio. Loading/unloading modules is a controller-side transaction with a module lease; no binary unload while active instances, callbacks or UI views reference it.
- Test 1/2/8/16 Acid slots with simultaneous modulation, MIDI, automation and voice load at 44.1/48/96 kHz, 64/128/512 blocks, including silent and active baselines. Report host parameter enumeration time, creation time, memory, worst-case audio callback load and xruns. Do not claim sample-accurate automation unless measured/proven.

## Test-first delivery order and gates

1. **Contract freeze first:** Rack owner approves ABI/package model, 96-vs-128 pool decision and endpoint/replacement policy. Acid agent supplies a machine-checkable 80-entry manifest and tests all unique identities/types/limits. Do not edit Rack core in #59.
2. **Common adapter:** Rack owner exports typed named parameter operations and host automation bridge, with missing-module preservation, upgrade and rollback regression tests. Acid agent removes competing host authority only after its compatibility suite passes.
3. **Independent Acid:** build an actual separate DLL/module and package with DSP, own descriptor, Sound/Pattern/Generator, the approved specialized UI and stable hash of the unchanged Rack binary; test install/remove/reinstall and two-slot isolation.
4. **DSP and UX:** tighten only proven RT hotspots; complete real 16-step editor, ENV/LFO/STEP/Play flags and performance controls. Preserve accepted Acid layout and avoid redesigning the common Rack.
5. **Production:** exact-head Windows Release/full CTest/package, investigate the Bass `save_restore_rerender` diagnostic with a clean baseline rather than waiving it, Cubase/REAPER host-automation state tests, visual 1024×768/1180×760/1500×920/125% DPI, audible A/B, and documented CPU/memory/latency/tail budgets.

**Do not mark PR #59 production-ready or merge it on the strength of green Acid unit tests alone.** The older Acid-specific IDs and static provider in this branch are interim only.
