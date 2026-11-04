# Native instrument handshake — Rack ↔ instrument owners (Acid #59, Bass)

Status: **PROPOSED by the Rack owner, 28.10.2026.** Needs agreement from the
Acid/Bass owner and the project owner. Rack side implemented in #68 (M0).
ABI: [INSTRUMENT_MODULE_ABI.md](INSTRUMENT_MODULE_ABI.md),
header `libs/instrument/abi/VoxModuleAbiV2.h` (proposed, not frozen).

Neither instrument is a module today: #59 Acid and #52 Bass are in-process
C++ providers linked into the Rack. M1/M2 need them as separate DLLs.

| # | Topic | Rack proposal | Owner of the work |
| --- | --- | --- | --- |
| 1 | Binary | Each instrument is its own CMake shared-library target exporting only `voxModuleEntryV2` from `VoxModuleAbiV2.h`. JUCE/STL may be used inside the DLL, never across it. No Rack sources linked in. | Instrument owner |
| 2 | Package | One signed `.vins` file ([VINS_PACKAGES.md](VINS_PACKAGES.md)): `manifest.json`, `manifest.sig`, `module/win-x64/<name>.dll` (+ resources later). The manifest's `instruments` must equal the module's descriptors. Build it with `vox_add_vins(... KIND native MODULE_TARGET <target>)`. | Instrument owner builds; Rack verifies and loads |
| 3 | Parameters | Every host control is a typed `VoxParameterDescriptorV2` with real units, ranges, defaults, choices and an explicit `poolIndex`. Indices never move or get reused; new controls append. Proposed: Acid 0..79 (its inventory), Bass 0..12. | Instrument owner declares; Rack binds |
| 4 | Pool size | Develop against an experimental 128-per-slot layout (Acid needs 80); `slotNN.p001..p064` stay stable. Final 96/128 only after Cubase/REAPER comparison. | Rack owner + project owner |
| 5 | Acid specifics | `waveform`: float if the DSP blends continuously, choice only if it switches. The four packed play flags become four boolean controls; packing stays a private state detail. | Acid owner |
| 6 | Macros | The eight Rack macros stay independent assignable performance controls, not copies of instrument controls. V2 descriptors carry no `preferredMacro`; default assignments, if wanted, go in the package manifest later. | Rack owner |
| 7 | Automation authority | The Rack alone registers host parameters, converts plain/normalized, handles gestures and saves values. The module receives plain values by descriptor index (`setAllParameters` snapshot + dirty `paramChanges`) and never reports values back. `AcidParameterBridge` and Acid APVTS registrations go away in the DLL. | Both |
| 8 | Realtime | No per-block reapplication of unchanged values (the host sends only changes), no string lookups, smoothing only where needed. Zero allocations/locks in `process`/`reset`. | Instrument owner |
| 9 | Transport | `VoxTransportV2` every block: tempo, ppq at sample 0, playing, loop, discontinuity, with validity flags. No tempo inference from sample position or wall clock. Acid LFO sync/STEP/sequencer use it. | Both |
| 10 | State | Module owns its binary state and its migrations (`loadState(stateVersion, bytes)` loads older versions, e.g. Acid v1, v3–v8). Rack stores bytes + version; named parameter values remain authoritative on restore. `maxStateBytes` declared honestly (Acid 4096, Bass 52). | Instrument owner |
| 11 | Content | Defined in V2.1: `VoxContentApiV2` (pattern caps, factory patterns) and the pattern snapshot appended to `VoxProcessContextV2`; see [PATTERN_CONTENT_ABI](PATTERN_CONTENT_ABI.md). Sound presets stay named values. | Rack owner (approved, #78) |
| 12 | UI | Rack's generic VOX controls (already in #68) first; declarative `ui.json` layout next; a native editor interface only after a reviewed requirement (Acid 303 sequencer interactivity). | Rack owner |

Please answer item by item on #68 or #59; disagreements change this table,
not the code silently.
