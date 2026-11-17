# Windows x64 VST3 — standalone suite build and validation

**Updated 2026-11-17.** This is the **active** build guide. The October Rack-only `VstEngine` build and `apps/vst3/` product are legacy donors, **not** the Drums release candidate. Source: [`.github/workflows/suite.yml`](../.github/workflows/suite.yml), [pinned dependencies and suite selector](ultima-vox-music/BUILD.md).

## Requirements

- Windows 10/11 x64, Visual Studio 2022 with C++ desktop development, CMake, Git.
- JUCE **9.0.1** pinned to `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, Steinberg VST3 SDK validator pinned in CI to `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0`.
- A clean local CMake configure may download pinned dependencies. The Windows CI uses checked runner-cache copies and `VOX_STRICT_DEPENDENCIES=ON`; see workflow. Offline plugin operation does not imply source builds work offline.
- Select `integration/vox-drums-1.0` or a feature branch **derived from it**, not `main` / `integration/rack-1.0`.

## Build the current standalone suite locally (PowerShell)

```powershell
git clone https://github.com/ultima-vox/vox-electronic-engine.git
cd vox-electronic-engine
git fetch origin
git switch -c dev/drums-finish origin/integration/vox-drums-1.0
git rev-parse HEAD

cmake -S . -B build-suite -G "Visual Studio 17 2022" -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF
cmake --build build-suite --config Release --parallel
ctest --test-dir build-suite -C Release -L suite -E suite_drums_gain_staging --output-on-failure
ctest --test-dir build-suite -C Release -L suite -R suite_drums_gain_staging -j 8 --output-on-failure
```

For CI parity also require strict verified dependency pins and run the **Steinberg VST3 validator** on each built suite bundle, as done by the workflow. The two CTest commands are intentionally separate: callback timing is part of the **serial** set, while eight independent gain cases run with `-j 8`. Do not run wall-time callback benchmarks concurrently with gain tests. The actual workflow builds, validates, writes WAV/listening and gain CSV reports, records SHA-256 and uploads artifacts.

**Expected named VST3 products in suite build:** `VOX Drums.vst3`, `VOX FX.vst3`, `VOX Trance Synthesis.vst3`. Building or validating an FX/Synth **shell** is not evidence of its DSP/feature completeness. Do not assume CPack/NSIS packaging or code signing is already production-approved.

## Evidence requirements

- Log exact SHA, runner and environment; distinguish CI cancellation/infrastructure failure from CTest assertion failures.
- [Integration #141 suite 43943639327](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43943639327) was **interrupted** on `f9d544662` after 16/32 serial cases due to a self-hosted runner shutdown signal; it is not a completed qualification.
- Earlier [gain staging #150 suite 43805453483](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/43805453483) passed its individual exact-head gates, but this cannot be substituted for post-merge integrated tests.
- Final automated acceptance covers Drums state/undo, host automation, Song & MIDI export, nine buses, all 16 pads, sample/kit loading, callback performance at 44.1/48/96 kHz, and real editor integration tests.
- **Manual Cubase/REAPER acceptance is one final owner stage**, after finished UI, sound/content/licensing and exact-HEAD CI. CTest and the VST3 validator cannot prove sound quality, genuine DAW drag/drop, host routing or visual acceptance.

For the complete current task and previous developer decision trail see [DEVELOPER_HANDOFF](DEVELOPER_HANDOFF.md).
