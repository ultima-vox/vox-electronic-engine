# Program trajectory editor — engineering checkpoint

Tested implementation: `381b625723d3c3be384edfae429314716b1dbe5e` on `arena/20b960e7-vox-electronic-engine`. PR #156 was not updated by this checkpoint. This is not Windows or DAW qualification.

- **Program curves** switches the existing native FX panel between four layer cards and a full-width Program trajectory graph. Amplitude, pitch, cutoff and panorama target the corresponding existing FPRG v4 fields. Each selection supports the existing node editing shortcuts; Undo, Redo and neutral Reset operate on the selected curve. Switching the trajectory clears its undo history. The edit is structural, for new events, not host automation.
- Program and layer edits now share a revision-guarded publication path. Invalid targets/curves do not publish; undo or a gesture from a superseded Program cannot write into the new one. No audio callback changes, VST identity changes, state-format changes, or host parameter additions.
- Store regression checks four distinct Program edits, layer isolation, invalid targets, and stale edits against replacement. DSP regression checks layer and Program pitch and cutoff independently. JUCE processor tests cover the view toggle, all four bindings, reset/undo and host state round-trip; they were syntax-checked, not executed here.

## Local evidence

Linux x86-64, GCC 12.2.0. Direct builds: `g++ -std=c++20 -O2 -pthread -Iproducts/fx tests/suite/<test>.cpp -o <test>`; 1 MiB stack at execution. `FxProgramStoreTests` used `-DVOX_TEST_INTERPOSE_MALLOC=1 -Ilibs/vox-testkit/probe -rdynamic` and linked `AllocationProbeLinux.c` compiled with `gcc -std=c11 -O2`. Selected ASan/UBSan runs used `-O1 -g1 -no-pie -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all` in place of `-O2`. Existing GCC `-Wstringop-overflow` diagnostics in the codec truncation test appeared during the sanitizer build; the executable completed with no sanitizer finding.

JUCE syntax-only: pinned `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, C++20, `JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1`, `JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`, `JucePlugin_Name="VOX FX"`, `VOX_SUITE_STATE_TAG="VXFX"`. Checked `Processor.cpp`, `FxEditor.cpp`, `LayerPanel.cpp`, `CurveEditor.cpp` and `FxProcessorTests.cpp`.

| Test | Result |
| --- | --- |
| `FxCurveTrajectoryTests-sanitized.log` | FX curve trajectories: 0 failure(s) |
| `FxCurveTrajectoryTests.log` | FX curve trajectories: 0 failure(s) |
| `FxGeneratorTests.log` | FX generator: 0 failure(s) |
| `FxOfflineRenderTests.log` | FX offline WAV: 0 failure(s) |
| `FxPlaybackModeTests.log` | FX playback modes and note ownership: 0 failure(s) |
| `FxProgramCodecTests-sanitized.log` | FX Program codec: 0 failure(s) |
| `FxProgramCodecTests.log` | FX Program codec: 0 failure(s) |
| `FxProgramStoreTests-sanitized.log` | FX Program store: 0 failure(s) |
| `FxProgramStoreTests.log` | FX Program store: 0 failure(s) |
| `juce-syntax.log` | PASS pinned JUCE syntax only (no execution) |

Still open: executable native editor verification and DPI/accessibility, multi-node/numeric editing, full source/filter controls, FX quality, curated content, exact-head Windows qualification and final manual DAW acceptance. `[RUN_FX_SUITE]` remains unchanged.
