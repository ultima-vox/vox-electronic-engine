# JUCE_PORTING_MAP · future native work only

**Gate:** obtain final owner visual approval under [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127) before any new Drums C++ UI. Target is native C++20 / JUCE; no React, WebView, Electron, Chromium or web asset runtime in releases. This file does not authorize ParameterID, ABI, DSP, state/routing, bus or VST3 identity changes.

For the **current native source audit**, use the [DS-00 reconciliation and exact-SHA binding matrix](../../../docs/ui/vox-music/README.md). The browser analogues and #125 examples below are historical porting context, not the current product baselines.

## Components and data seams

| Browser reference | Native placement | Binding and life-cycle |
| --- | --- | --- |
| `Knob` (rotary/bipolar/encoder/macro), `Fader`, `Slider`, `NumericValue`, `RangeSlider` | Extend conceptually `vox::ui::VoxKnob` (`libs/vox-ui/components`); candidate native names `VoxFader`, `VoxValueField`, `VoxRangeSlider`. Shared custom paint over JUCE input/accessibility primitives only. | Use existing velocity/normalization/range/skew/text/default from the actual `RangedAudioParameter`. Product-side JUCE attachment owns the host gesture and parameter readback (Drums APVTS; FX direct RangedAudioParameter bindings). A bipolar center is normalized from the plain center, not assumed .5 for every range. Effective modulation is separate display-only state. |
| `Button`, `Toggle`, `Switch`, `Select`, `Segmented`, `Tabs`, `Module`, `Splitter` | Existing `VoxButton`, `VoxComboBox`, `VoxTabBar`, `VoxPanel`/`VoxSectionHeader`, proposed `VoxSplitter`. | Boolean/choice attachments for real host params; controller commands and bounded UI state for layout. Keyboard/focus and change notification distinct from rendering. |
| `Pad` / Grid/List, `Keyboard`, `TriggerButton`, `XYPad` | Proposed `VoxPad`, `VoxKeyboard`, `VoxXYPad` primitives plus product Pad Rack controller. | Separate edit selection, audition and MIDI paths. Audition event carries note, source and velocity; does not change selection. The two XY parameters get explicit gestures, not a hidden second store. |
| Curve/Waveform/Envelope/Filter/Timeline | Proposed `VoxCurveEditor` / `VoxWaveform` and graph view components. Product editor subscribes to actual model/decoded snapshot. | Non-RT structural edits with begin/live/commit on immutable state; selection and preview are not flattened automation. Pointer-safe lifetime for sample content and peak views. |
| Meter / OVER / status | Proposed `VoxMeter` and `VoxStatusLED`, with a product-owned telemetry coordinator. | A single bounded UI reader drains each `takePadMeter`, `takeMainMeter`, `takeHitMask` **once**, then fans out immutable snapshots. No pad consumers competing to `exchange`. Native hit velocity and per-bus OVER measurement need explicit source-verified capability or labelled absence, not a guess. |
| `StepGrid`, parameter lane, `SongTimeline`, Inspector | Product editors composing shared grids/cues and actual `PatternStore`/`SongModel`/`KitStore`, not a second baseline store in `libs/vox-ui`. | Stable slot/variation/step/bar reference. `Edit This Bar` clones on first actual edit, undo restores original link. Pin tracks a stable object reference or reports invalid; select-only does not audition. |
| PresetBrowser, ModulationTable, Dock | Shared presentation primitives; product-owned APIs for presets/routing/program/allowed zones. | Loaded assets and immutable Program preparation off RT. One panel instance, one canonical model. Versioned bounded UI layout. Review DLAY/serialization scope against current release policy; do not import React JSON into VST state or add unreleased DUIS fallback solely for #125. |

### Parameter examples (source-verified, not code edits)

See [#125 candidate `parameters.csv`](https://github.com/ultima-vox/vox-electronic-engine/blob/c7c29bbbd6101dfb5e0cb2418a071d11caeb15b2/products/drums/parameters.csv) and [DS-00 binding matrix](https://github.com/ultima-vox/vox-electronic-engine/blob/065d2be5627be6bac10098b2dbedf6fe65944807/docs/ui/ds-00/UI_BINDING_MATRIX.csv). The `demo.*` identifiers are local UI keys. Read actual ranges/defaults from the attachment, not browser convenience values or display strings.

| Control | Authoritative native owner | Notes |
| --- | --- | --- |
| Level / Tune / Decay / Pan / Cutoff / Mute ×16 | `drm.p01`…`drm.p16` `level`, `tune`, `decay`, `pan`, `cutoff`, `mute` | Same attachment source across Inspector, Sound, Pad Rack and Mixer. Level 0–1 linear (UI %); pad 1 default .85. Tune ±24 st, Decay .01–10 s, Pan −1…1, cutoff 20–20000 Hz. Preserve the native JUCE skew for Decay/Cutoff (`setSkewForCentre` at the geometric mean) and real per-pad defaults. |
| Solo & Sends ×16 | `drm.pNN.solo`, `.send1`, `.send2` | Read actual taps; sends are not gain or mute aliases. |
| Kit Level | `drm.kit.volume` | Pre-routing, multiplied at new voice trigger in `KitEngine`. All Main/Out2–9. The Main L/R display is measured separately (post returns, pre host), not a gain parameter. |
| Returns | `drm.fx.delay.{time,feedback,tone,level}`, `drm.fx.reverb.{size,damp,level}` | Native stages own time, parameter and tail behavior. No parity claim with the browser delay/convolution. |
| FX layer bases and macro paths | [Current FX rows in DS-00](../../../docs/ui/vox-music/UI_BINDING_MATRIX.csv): 19 registered parameters (`FxProcessor::addParameter`, not APVTS) | Eight `fx.macro.N` bases are 0–1 with .5 default and bipolar DSP mapping. Family/types, curves and routes are structural Program. Set playback/display from actual native data, never a borrowed Rack pool or browser IDs. |

### #125 editor inventory that must be reconciled before native implementation

| Candidate surface | Target in #127 | Browser status / follow-up |
| --- | --- | --- |
| Performance | Pad Rack, top bar, Diagnostics and single Mixer | Play/silent select, local kits, Panic, Kit Level, polyphony/choke are represented. Kit velocity scale/hat choke, actual preset chooser and host mode need exact native bindings. |
| Sound | Full Sound, Sample and contextual Inspector | Exact basic knobs, cutoff/Drive, source and A/B trim demo. Native Fine, Character/model choice, filter model/resonance, transient/sustain, pitch env/depth/decay, sub/tail and velocity-layer/simultaneous A/B are not implemented by Web Audio. Reconcile model-dependent parameters/roles/players and prepare loaded sample assets off RT, not generic placeholders. |
| Pattern / Song | Step and Song editors | Shared edits, parameter lanes, linked vs unique, copy/undo, local transport, downloadable first-pass MIDI. Preserve native Seq Mode (Host MIDI/Internal/Both), A/B, swing, accent, lane flags and whole-Song timing/export; test in hosts. |
| Mixer | One Mixer Dock | Browser values/taps are real local audio but do not certify native gains, nine host enabled/fallbacks or tail behavior. |
| Generate | Generator dock | 23 names and eight amounts, seed and locks; no native generator parity. Bind current canonical style data and proposals/actions from actual generator. |
| Routing | Routing dock | Note/output, local monitored buses. Use actual Host MIDI/readback, bus presence, fallback and routing contracts in native. |
| Presets | Library dock and product preset controller | Browser session snapshots only; wire native preset selectors, import/export, missing assets and Undo Load. |
| Advanced | Diagnostics dock | Local voices/RMS/Panic, no native timing/CPU/performance status. Map only safe native snapshots. |

## Transaction, lifetime and real-time policy

1. **Editor construction / rebind:** validate product IDs and pull an initial processor/Program snapshot before making controls interactive. Hold attachments in product controller objects. On selection change, end any active pointer/keyboard gesture, detach obsolete listeners, bind the new identity, push actual state. Layout changes only move views.
2. **Host parameter:** from native slider/button/choice or numeric commit, begin change gesture once, make normalized writes through the current attachment, end once on release/cancel/blur. Repaint base on message-thread readback. Never write modulated values into base. Text edit uses the parameter's actual unit parser. DAW automation/restore has explicit precedence and does not cause feedback writeback.
3. **Structural edit:** copy/validate patch off the audio thread, publish bounded immutable state; coalesce a gesture to one undo item. Use stable pin refs, reject invalid loaded targets, and preserve source/link semantics. Prepare/decode/reclaim buffers outside RT. No network/IO/locks/heap work in callbacks.
4. **Telemetry:** publish measurements with named points and units; drain on a bounded message-thread cadence. Do not use a `take*` method in individual paints, and do not imply peak means device clip. Distinguish UI selection, event trigger and signal sample paths.
5. **Editor close / reopen:** finish gestures, disconnect attachments, unregister UI timers/listeners and safely retire prepared state. Processor owns playback and musical state independent of editor lifetime. Restore approved layout without changing DSP or host state.
6. **Acceptance:** after visual sign-off and independent architecture review, implement a native vertical slice first. Browser runs do not offset any of the native checks below. Capture only with the owner's authorization.

## Standalone Drums #141 — checklist after Gate 1

No native work before explicit owner visual approval under [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127) and [#135](https://github.com/ultima-vox/vox-electronic-engine/pull/135). Integrate in [#141](https://github.com/ultima-vox/vox-electronic-engine/pull/141), rechecking its exact head against [DS-00](../../../docs/ui/vox-music/DS00_RECONCILIATION.md). Use one dock workspace with native JUCE controls; no WebView or return to eight pages.

| Surface | Native binding and acceptance |
| --- | --- |
| Pad Rack, Inspector, Sound, Mixer | Bind rotary/fader/numeric views through the existing `DrumsProcessor` `VOXDRUMS` APVTS and DS-00 IDs/units/defaults. One begin/change/end per gesture, no automation writeback. Keep 16 pads, Select ≠ Audition and pinned edit targets. |
| Transport, Step/Song, MIDI | Use processor host playhead and native `BarTiming` for tempo/PPQ, supported meters and fallback; exercise seek/loop and Host MIDI/Internal/Both. Verify Pattern/full-Song MIDI timing and `performExternalDragDropOfFiles` with editable notes in Cubase and REAPER. Do not port the fixed-4/4 browser writer. |
| Routing and one Mixer | Bind Main + Out 2–9 to nine real stereo host buses, verify `activeOutputs()` and disabled aux → Main without rewriting the stored route. `KitEngine` applies Kit Level on trigger before all routes; sends return to Main. Label actual tap points; don't infer host meters from Web Audio. |
| Sound and Sample | Bind model-specific structural controls and A/B modes through the pad controller, not demo IDs. Preserve `DSMP` sample references; verify imported/missing assets, restore and off-RT decode, peak preparation and reclamation. |
| Song, kits and presets | Use native Pattern/Song and preset stores, copy-on-first-change and Undo/Undo Load; verify linked bars, portable kits and project/preset round trips with existing state codecs. Keep UI layout separate from musical/host state, never import browser localStorage into VST. |
| Windows/DAW gate | Verify the real editor at 1040×700, 1280×720, 1440×900 and 1600×1000 at 100/125/150/200% DPI. Check resize, focus/keyboard, automation, state, routing and real-time behavior; require exact-head Windows CI/CTest and Steinberg validator plus owner DAW acceptance and independent TL review before any merge. |
