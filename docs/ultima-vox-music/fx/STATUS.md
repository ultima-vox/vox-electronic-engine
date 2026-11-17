# VOX FX development status — Draft PR #156

This is a resume record, not release acceptance. Exact-head Windows suite,
validator and binary SHA are pending; HOLD prohibits merge and release.

| Milestone | Status |
|---|---|
| FX-01 | In progress: playable noise riser code, gain/duration parameters, v1→v2 state migration, interim bound UI and processor tests. Processor/UI tests await an executable JUCE suite build. |
| FX-02 | Partial: seconds and bars clock/curve primitives, 4-layer noise engine with per-event snapshot, and single-producer bridge tests. Seconds timing and validated layered Program persistence/publication are wired to plugin; basic layer enable/family/gain/pan/timing UI and per-layer gain-curve editing exist; pan/pitch/filter/global curve UI, bar sync and event policies remain. |
| FX-03 | Partial: Noise Riser, tonal/noise Downlifter, six-partial Shepard sweep, Impact, Whoosh, Laser, Squelch, Metallic, Texture, Formant, Glitch and a procedural-source Granular Cloud are selectable for new events and persisted as structural state. User sample import, full host-synced Glitch, granular controls and comprehensive quality tests remain. |
| FX-04 | Partial: standalone typed 64-route matrix, LFO and seeded sample-and-hold primitives; no plugin modulation integration, macros, effects or quality policies yet. |
| FX-05 | Partial: deterministic standalone Generate/Mutate/A-B and a 48-program procedural seed corpus with locks and audible smoke tests. Not yet connected to plugin/UI or a preset browser; musical curation, full settings, sample handling, patterns and exports remain. |
| FX-06 | In progress: truthful interim Perform panel; other workspaces and controls remain. |
| FX-07 | Not complete: Windows exact-head suite/validator and binary hash, host, performance, safety, licensing and packaging qualification. |

Next: execute the new processor and layered-state integration tests on Windows,
add pan/pitch/filter/global curve editing and executable editor tests, then implement the remaining
family-specific sound engines and complete the full modulation/FX inventory. Validate the pending
Windows runs as they finish, fix failures and repeat on exact candidate SHA.
