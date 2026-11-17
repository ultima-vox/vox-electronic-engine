# VOX FX development status — Draft PR #156

This is a resume record, not release acceptance. Exact-head Windows suite,
validator and binary SHA are pending; HOLD prohibits merge and release.

| Milestone | Status |
|---|---|
| FX-01 | In progress: playable noise riser code, gain/duration parameters, v1→v2 state migration, interim bound UI and processor tests. Processor/UI tests await an executable JUCE suite build. |
| FX-02 | Partial: seconds and bars clock/curve primitives, 4-layer noise engine with per-event snapshot, and single-producer bridge tests. Seconds timing and validated layered Program persistence/publication are wired to plugin; basic layer enable/family/gain/pan/timing UI and per-layer gain-curve editing exist; pan/pitch/filter/global curve UI, bar sync and event policies remain. |
| FX-03 | Partial: Noise Riser, tonal/noise Downlifter, six-partial Shepard sweep, Impact, Whoosh, Laser, Squelch, Metallic, Texture, Formant, Glitch and a procedural-source Granular Cloud are selectable for new events and persisted as structural state. User sample import, full host-synced Glitch, granular controls and comprehensive quality tests remain. |
| FX-04 | Partial: standalone typed 64-route matrix, LFO and seeded sample-and-hold primitives; no plugin modulation integration, macros, effects or quality policies yet. |
| FX-05 | Partial: deterministic standalone Generate/Mutate/A-B and a 48-program procedural seed corpus with locks and audible smoke tests. Not yet connected to plugin/UI or a preset browser; offline canonical 1/16 trigger scheduler, 12 seed patterns and SMF export exist but are not integrated; musical curation, full settings, sample handling, host sync and user-facing file exports remain; worker-only 24-bit/float WAV bytes render through the current engine, without nonexistent effects tails. |
| FX-06 | In progress: truthful interim Perform panel; other workspaces and controls remain. |
| FX-07 | Not complete: Windows exact-head suite/validator and binary hash, host, performance, safety, licensing and packaging qualification. |

Resume: Wire the existing standalone generator, pattern scheduler and WAV/MIDI
render foundations to validated Program/state and real user-facing workflows;
implement the specified modulation, four true FX slots and host-sync behavior;
complete family algorithms, sample ownership and the remaining curve/UI paths.
Run the processor/editor tests in an executable JUCE build, inspect and fix the
queued Windows suite/build runs, and repeat all qualification on the exact final
head. The 48 procedural programs and 12 patterns are uncurated seed corpora,
not accepted factory content. No host or installation acceptance is claimed.
