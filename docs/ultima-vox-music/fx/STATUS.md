# VOX FX development status — Draft PR #156

This is a resume record, not release acceptance. Exact-head Windows suite,
validator and binary SHA are pending; HOLD prohibits merge and release.

| Milestone | Status |
|---|---|
| FX-01 | In progress: playable noise riser code, gain/duration parameters, v1→v2 state migration, interim bound UI and processor tests. Processor/UI tests await an executable JUCE suite build. |
| FX-02 | Partial: seconds and bars clock/curve primitives, 4-layer noise engine with per-event snapshot, and single-producer bridge tests. Seconds timing and validated layered Program persistence/publication are wired to plugin; basic layer enable/family/gain/pan/timing UI and per-layer gain-curve editing exist; pan/pitch/filter/global curve UI, bar sync and event policies remain. |
| FX-03 | Partial: Noise Riser, tonal/noise Downlifter, six-partial Shepard sweep, Impact, Whoosh, Laser, Squelch, Metallic, Texture, Formant, Glitch and a procedural-source Granular Cloud are selectable for new events and persisted as structural state. User sample import, full host-synced Glitch, granular controls and comprehensive quality tests remain. |
| FX-04 | Partial: standalone typed 64-route matrix, LFO and seeded sample-and-hold primitives; four serial shared slots with bypass, lowpass, drive, wavefold, bitcrush, ring, gate, a fixed-rate stereo phaser/flanger/chorus, single-tap delay and bounded multitap reverb are wired into audio, basic UI and versioned structural state. Eight stable slot wet/amount parameters are host-automatable and stored in FXSL v2 (v1 migrates). Eight host-automatable macros, two fixed-rate LFOs and seeded sample-and-hold can route to master gain or stereo balance through up to 64 typed routes; DSP, state and basic editor are wired. Other sources/destinations, adjustable/synced motion rates, reverb quality/controls, delay feedback/tempo sync, configurable modulation-effect controls and quality policies remain. |
| FX-05 | Partial: deterministic standalone Generate/Mutate/A-B and a 48-program procedural seed corpus with locks and audible smoke tests. Generate/Mutate now publish to the plugin Program and have basic seed controls; seed and layer-lock masks are saved in FGEN v1 and restored in the UI; an interim selector loads the 48 uncurated procedural seed Programs without overwriting automation; session-only full-state A/B store/recall is exposed with explicit host-value recall; fine-grained lock UI, full generator settings, persistent A/B and a curated preset browser remain; 1/4–1/32 straight/triplet 4/4 host-PPQ trigger playback, 12 seed patterns and canonical SMF export now have a processor controller and FPAT v3/FPEN project state; basic 1–4-bar length/grid/page and 16-step on/off plus note/velocity/probability/ratchet and swing editing exist; a save-dialog action exports the selected trigger pattern to a .mid file at fixed 120 BPM; full workspace, musical curation, full settings, sample handling and host-sync validation remain; a 48 kHz/24-bit C4 WAV save action snapshots Program and FX/host base values and renders on a cancellable editor-owned worker with bounded filter/modulation and serial delay tails; WAV format/options, drag export and Windows dialog/worker validation remain. |
| FX-06 | In progress: truthful interim Perform panel; other workspaces and controls remain. |
| FX-07 | Not complete: Windows exact-head suite/validator and binary hash, host, performance, safety, licensing and packaging qualification. |

Resume: Complete generator settings, fine-grained lock UI, persistent A-B and curated preset/content integration; wire
the remaining pattern grid/tempo behavior and WAV/MIDI render foundations to
real user-facing workflows;
implement the specified modulation, remaining FX algorithms and host-sync behavior;
complete family algorithms, sample ownership and the remaining curve/UI paths.
Run the processor/editor tests in an executable JUCE build, inspect and fix the
queued Windows suite/build runs, and repeat all qualification on the exact final
head. The 48 procedural programs and 12 patterns are uncurated seed corpora,
not accepted factory content. No host or installation acceptance is claimed.

Windows evidence: older runs 44172558893, 44171848293 and 44171634526
failed with four reproducible SEGFAULT test processes (noise riser, processor,
layer and Shepard), not runner outages. Large test objects were moved to heap
as a plausible fix but the hypothesis is not verified until an exact-head
Windows suite passes; isolate surviving failures with crash traces. CI queue
cancellation was proposed separately in PR #155; FX does not change workflows.

Skills bridge: inspected the three Markdown adaptations and Arena instructions
at `agents/arena-skill-bridge-20261118@d92d322a0` read-only, plus root
`AGENTS.md`, the agent contract and FX specification. No native skill runtime
or subagent facility was available or claimed; bounded implementation and
independent verification were performed sequentially.
