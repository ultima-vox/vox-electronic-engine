# VOX FX development status — Draft PR #156

This is a resume record, not release acceptance. Exact-head Windows suite,
validator and binary SHA are pending; HOLD prohibits merge and release.

| Milestone | Status |
|---|---|
| FX-01 | In progress: playable noise riser code, gain/duration parameters, v1→v2 state migration, interim bound UI and processor tests. Processor/UI tests await an executable JUCE suite build. |
| FX-02 | Partial: seconds and bars clock/curve primitives, 4-layer noise engine with per-event snapshot, and single-producer bridge tests. Only seconds/noise default is wired to plugin; layered Program persistence/controller, bar sync, complete curves and event policies remain. |
| FX-03 | Partial: Noise Riser, tonal/noise Downlifter and six-partial Shepard sweep are selectable for new events and persisted as structural state; nine additional families, grains and full quality tests remain. |
| FX-04 | Not started: modulation matrix, macros, effects and quality policies. |
| FX-05 | Not started: generator, content, preset/sample handling, patterns and exports. |
| FX-06 | In progress: truthful interim Perform panel; other workspaces and controls remain. |
| FX-07 | Not complete: Windows exact-head suite/validator and binary hash, host, performance, safety, licensing and packaging qualification. |

Next: connect validated structural Program state to the bridge through a
single-producer controller with explicit full-queue retry; ensure state restore
and audio publication are transactional. Add actual layer/pan/gain curve edits
and tests, then implement family-specific sound engines. Validate the pending
Windows runs as they finish, fix failures and repeat on exact candidate SHA.
