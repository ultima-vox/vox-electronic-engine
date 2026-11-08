# 01 — Shared Platform & Architecture (ADR-VPS-001)

**Дата:** 08.11.2026. **Статус:** proposed. **Цель:** три самодостаточных VST3-инструмента с общими DSP/UI/тестовыми библиотеками без общего runtime Rack.

## A. Verified baseline / что действительно есть

Проверено в `ultima-vox/vox-electronic-engine`, `integration/rack-1.0` HEAD `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`:

| Факт | Источник |
|---|---|
| `CMAKE_CXX_STANDARD 20`; `FetchContent(JUCE)`; `juce_add_plugin(VstEngine FORMATS VST3)` | `CMakeLists.txt` |
| Набор `vox::ui`: токены, `VoxLookAndFeel`, controls, showcase | `libs/vox-ui/*`, `docs/ui/DESIGN_SYSTEM.md` |
| Существующий барабанный движок: 8 пэдов, 16 voices, sample/synth/layer, voice/choke/состояние | `libs/drums/VoxFullOnDrums.c` и тесты |
| Примитивы DSP (BLEP oscillator, SVF LP, сглаживание, ADSR), без зависимости от UI | `libs/dspcore/Primitives.h` |
| Эффекты distortion/wavefolder/phaser/flanger/chorus/bitcrusher/delay/reverb | `libs/effects/EffectsChain.h` |
| Общий Rack native `.vins` ABI V2.4 и 128×16 host endpoints | `libs/instrument/abi/VoxModuleAbiV2.h`, `docs/architecture/RACK_PARAMETER_CONTRACT.md` |
| VOX FX FX-00 задокументировал host/ABI пробелы, но не добавил код | PR #110, HEAD `60929c206bd2b8d5cd8789cbcfa7fdfa17ee7059` |
| Производительность Rack+Drums под высокой нагрузкой — открытый P1 | PR #109; не считается исправленным в новой архитектуре автоматически |

Не переносить в новые standalone-инструменты старые Rack-specific ограничения (`128×16`, per-slot macros, `.vins` DLL scan, 16 slot graph) как обязательные продуктовые. Они могут быть полезным историческим кодом/тестами, но не моделью runtime.

## B. ADR: одна кодовая база, три бинарных продукта

### B1. Monorepo рекомендуемая организация

```text
ultima-vox/vox-electronic-engine/             # существующий репозиторий
  legacy-rack/                               # старый код — не перемещать без проверки CI/путей
  libs/
    vox-dsp/                                 # C++20, JUCE-free, тестируем отдельно
      oscillators/ filters/ envelopes/ modulation/
      drums/ acid/ bass/ fx/ effects/
    vox-audio-runtime/                       # voice allocation, transport, MIDI, buffers
    vox-preset/                              # versioned patch codec, validation, factory index
    vox-ui/                                  # существующая и расширяемая JUCE design system
    vox-testkit/                             # golden render, allocation, host integration
  products/
    trance-synthesis/
      Processor.* Editor.* Parameters.* Presets/ CMakeLists.txt
    drums/
      Processor.* Editor.* Parameters.* Kits/ CMakeLists.txt
    fx/
      Processor.* Editor.* Parameters.* Programs/ CMakeLists.txt
  cmake/  tools/  tests/  docs/
```

Пути — **предлагаемая** структура, не утверждение, что они уже есть. Первой задачей обязательно построить карту зависимостей и решить минимально инвазивное размещение, избегая ненужных перемещений старых исходников и поломки CI. Пригодный компромисс: `apps/vox-drums`, `apps/vox-fx`, `apps/vox-trance-synthesis`, `libs/vox-dsp`.

### B2. Слои зависимостей

```text
VOX Synthesis VST3 -> Synth Processor -> VOX Synth DSP -------+
VOX Drums VST3    -> Drums Processor -> VOX Drums DSP --------+-> common vox_dsp
VOX FX VST3       -> FX Processor    -> VOX FX DSP -----------+
          |                       |
          +-> VOX Editor/LookAndFeel (JUCE) --> vox_ui
          +-> Host params, preset bridge, transport, MIDI (JUCE)
```

- Каждый `PluginProcessor` — **обычный JUCE `AudioProcessor`**, созданный в одном статически собранном VST3.
- DSP обрабатывает planar float (опционально double после отдельного теста) и не включает JUCE GUI, filesystem и сетевой код; юнит-тесты могут запускаться вне плагина.
- Common libraries компонуются **статически** в каждый VST3; никакого динамического обращения к общей DLL или к Rack.
- Вызовы `prepareToPlay`, `processBlock`, `releaseResources`, `getStateInformation`, `setStateInformation` реализуются отдельно на каждый продукт; состояние компонента также автономно.

## C. Framework decision matrix

| Критерий | JUCE 9 | iPlug2 | Steinberg VST3 SDK непосредственно |
|---|---|---|---|
| VST3 + GUI + host integration | Да | Да | Да, больше glue-кода |
| Переиспользование текущего проекта | **Высокое** | Низкое/среднее | Низкое |
| Пользовательская графика | `LookAndFeel` и `Component` | IGraphics | VSTGUI или свой GUI |
| Портативность VST3/AU/AAX | Есть экспортные механизмы, проверять по формату | Есть | Требуются дополнительные wrappers/проверки |
| CLAP | Проверить поддержку на закреплённом JUCE 9; не объявлять готовым | заявлен в upstream | отдельная интеграция |
| Лицензия | AGPLv3 или коммерческая | zlib-like | VST3 SDK 3.8 MIT |
| Рекомендация | **Основной** | Резервный при неприемлемой лицензии JUCE | Не внедрять ради этих продуктов |

**Источники на 08.11.2026:**
- JUCE License: https://github.com/juce-framework/JUCE/blob/master/LICENSE.md ; https://juce.com/legal/juce-9-licence/ (внимание: **AGPLv3**, не «обычная GPLv3»).
- JUCE commercial tiers: https://juce.com/get-juce/ — условия и лимиты проверить перед оплатой/продажей, не закладывать стоимость без актуального предложения.
- iPlug2: https://github.com/iPlug2/iPlug2 — permissive zlib-like license.
- VST3 3.8: https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/VST3%2BLicense.html — MIT.
- CLAP: https://github.com/free-audio/clap ; https://github.com/free-audio/clap-juce-extensions — отдельный validation path.
- AAX: https://developer.avid.com/aax/ — SDK/коммерческая подпись отдельно, не обещать поддержку на Windows release.

**Решение:** JUCE retained. Если JUCE commercial licensing не подходит, оценить вынужденную миграцию в iPlug2 на одном test plugin, а не распиливать сразу три VST.

## D. Форматы и платформа

### D1. Phase 1 (обязательная)
- Windows 10/11 x64; MSVC 2022 release; VST3 instrument; Cubase 12/13/14/15 и REAPER x64 на согласованных актуальных сборках (уточнить реально поддерживаемые версии во время acceptance).
- Стандартные stereo main outputs; дополнительный multibus output только там, где это реально нужно (Drums), объявляется хосту корректной шиной, проверяется host bus activation.
- MIDI 1.0 NoteOn/Off, velocity, pitch bend, mod wheel, sustain, CC120/123, all-notes-off, Panic; нормальная реакция на Stop/Seek/Loop и удаление/замену пресета.
- Sample accurate note scheduling в рамках предоставленных `MidiBuffer.samplePosition`; точность автоматизации зависит от формата/доставки хостом: нельзя обещать sample-accurate automation без измерения.

### D2. Phase 2 (не блокирует Windows 1.0)
- macOS Intel/Apple Silicon VST3+AU, отдельный Apple CI, codesign, notarization и тесты Logic/Cubase/REAPER.
- CLAP при наличии подтвержденной сборки/validation, без зависимости релиза VST3 от CLAP.
- Standalone — отдельный wrapper/audio-device lifecycle, MIDI device selection, backend validation.
- AAX — отдельная коммерческая и инженерная программа с Avid SDK/iLok/signing, не обязательна по умолчанию.

## E. DSP realtime contract

**Основное правило:** `processBlock` и все вызываемые им функции не выделяют и не освобождают память, не захватывают mutex/spinlock, не вызывают файловую систему/лог/сеть, не декодируют WAV, не выполняют динамическую загрузку, не меняют UI и не делают долгих unpredictable операций.

1. `prepareToPlay`: allocation of voice arrays, grain pools, oversampling filters, delay/reverb histories, scratch buffers for declared maximum block/sample rate. Validate memory budgets and fail explicitly.
2. `processBlock`: bounded loops only; no `push_back` beyond capacity; noexcept or caught internal faults at boundary; finite math; denormal protection; DC filter/headroom; deterministic ordering of MIDI events (timestamp and stable tie-break).
3. Message thread: file chooser, sample decode, preset JSON, waveform mipmap, palette, IR load, generator preset building; publish **immutable prepared state** to audio via bounded lock-free or generation-scoped handoff. Disposal of old state also off audio; atomic refcounts can be unsafe if destructor/free runs on audio.
4. Param values: cache stable parameter IDs, use `getRawParameterValue` atomics for regular parameters; `SmoothedValue` / dedicated smoothing for continuous changes. Distinguish modulation from parameter base; never call UI methods from audio thread.
5. Structural state changes (osc algorithm, filter topology, voice count, sample replacement): prepare new state off audio; apply at safe event boundary or short bounded crossfade if feasible. Do not reallocate for type changes in `processBlock`.
6. Voice lifecycle: bounded allocator and deterministic stealing, fixed maximum, sample-safe sample ownership, Panic clearing notes, optional FX tails per explicit policy.
7. Realtime event queue always bounded; overflow is reported as an error/diagnostic, not silently ignored. Host API may not guarantee unbounded events; test realistic + worst-case bounded stimulus.

### E1. Target performance method (not fabricated benchmark results)

- Hardware baselines: owner-selected current DAW PC and at least one older self-hosted Windows runner; record CPU, SIMD, power mode, OS, compiler, binary SHA.
- Matrix: 44.1/48/96 kHz × 64/128/512 frames; single-instance and multi-instance; empty and typical musical patches + peak stress, repeated A/B runs on same runner.
- Capture p50, p95, p99, p99.9, max callback processing time; CPU as % block budget; deadline misses, thread allocation, dropped MIDI, latency and memory.
- Suggested **acceptance objective** (owner must approve after baseline): at realistic production load on target DAW PC, zero repeatable deadline misses across repeated controlled tests and p99 < 70% deadline to retain scheduling margin. This **is a target, not an achieved metric**; stress may legitimately exceed the playable hardware envelope but must be classified and documented.
- A green CI build alone proves neither glitch-free audio nor playable 96k/64 on all hardware. Failed runner provisioning/artifact upload is **infrastructure**, not a DSP failure; classify using per-job logs.

## F. DSP audio & gain policy

- DSP internal headroom: oscillator/voice stages normalized without clipping; proposed per-instrument output trim and optional soft peak protection (real processing, bypass selectable where viable).
- Avoid uncontrolled phase accumulation and NaN/Inf; `std::isfinite` validation of parameters, render assertions, denormal handling.
- Parameter smoothing baseline: 2–20 ms for gain/most continuous controls, cutoff in log-frequency domain, click-free bypass 5–20 ms; pitch modulation can require faster audio-rate path, but no discontinuities in phase.
- Anti-alias on saw/pulse/harsh nonlinearities; 2×/4× oversampling only where measured, with correct reported latency where nonzero.
- Gain reduction and RMS/peak meters derived from actual processed buffers, exposed through thread-safe snapshots. UI meters never drive DSP.
- Check 0 dBFS safety and test true-peak overs after sample-rate conversion as an additional quality gate, not claim guaranteed clipping immunity.

## G. Parameter & preset/state contract

**Каждый VST3 имеет собственные стабильные `ParameterID`**. Не копировать slotNN IDs из Rack. JUCE APVTS обеспечивает DAW automation и один authoritative parameter bank. Разные продукты могут иметь разные количества параметров — 128 больше не предел.

- Состав параметров фиксировать до публичного 1.0; изменения ID/диапазона/дискретности после релиза требуют versioned migration and tests.
- Host automatable: audible base controls, macro 1–8, bypass/sync/generator-performance. Structural controls (osc type, modulation matrix, pad routing, waveform references) обычно сохраняются в versioned patch state; автоматизация topology допускается только после строгой RT реализации.
- Для параметров применять JUCE `AudioParameterFloat`, `AudioParameterBool`, `AudioParameterChoice` и `AudioProcessorValueTreeState::ParameterLayout`; UI attachments держать живыми только пока редактор существует.
- Patch/Preset = format version + product ID + engine version + canonical parameter values + non-scalar program content + sample references or licensed embedded factory blobs + RNG seed + generation settings.
- DAW save/restore must be transactional: parse → validate → prepare → commit, preserving old state on error. Checksum optional for corruption detection, not substitute for boundary checking.
- Preset loading UI must not block processing; asset load async; unresolved file makes an explicit missing sample state and preserves reference, no sound substitution disguised as success.
- User presets stored under app-data with deterministic name/version collision policy; factory presets installed read-only. Path and license review prior to shipping.
- Undo/Redo applies to UI model (DAW host params through appropriate gestures, structural program via command transactions); A/B comparisons do not silently overwrite host automation.

## H. MIDI & transport contracts

- **Trance Synthesis:** poly or mono/legato mode, note priority, noteOff owner, pitch bend, pedal, parameter changes during held notes, deterministic arp/step timeline. MIDI export of generated patterns as SMF; actual note export routed to host only when host capability is verified (VST3 hosts differ).
- **Drums:** note mapping per pad, velocity ranges, note stack, choke group, CC120/123, Panic, sample voice finish, loop/restart. Export matches played step event sample/tick timing.
- **FX:** one-shot/gate/overlap/retrigger; sample-exact start; deterministic PPQ/bar-duration; tempo changes during event policy; noteOff may release or be ignored by selected one-shot mode.
- All: timestamp order stability, transport stopped vs playing, PPQ unavailable fallback, tempo discontinuity, seek/backward position, loop, bypass and project reload. No stuck notes or double-trigger after restored state.

## I. Security/content

- Signed installer / verified payload hashes; do not place private signing keys in GitHub, CI logs, code, files supplied to agents or chat. Windows Authenticode decision independent from VST3 technology.
- Factory content: explicit origin, SPDX-compatible usage / CC0 or approved license, author attribution where required; avoid unclear Freesound terms and derivative repackaging. Store a content manifest with SHA-256 and licenses.
- User WAV decode must reject malformed headers, huge allocations, path traversal in archives, invalid sample-rate/size/channels; decode offline. No arbitrary code execution or script engines in presets.
- Installed dependencies are pinned by commit/tag/checksum (JUCE, SDK, optional libraries), not retrieved from mutable branch on production release.

## J. CI / release gates per VST3

1. Windows self-hosted runner: CMake configure MSVC Release, clean compile, `/W4` (or agreed level), clang-format/check, core unit tests.
2. DSP golden-render tests with tolerances documented; tests across 44.1/48/96k and 64/128/512, null/oversized/nonfinite MIDI and parameter cases.
3. Audio thread allocation test, concurrency and state fuzzing (ASan/UBSan on supported toolchains, TSan where supported; Windows-specific guard as needed).
4. Pluginval (where applicable), Steinberg VST3 validator, smoke load/plugin discovery, parameter ID/state/preset round-trip, sample and multibus behavior.
5. Host acceptance: Cubase and REAPER x64 on exact unchanged candidate binary (SHA-256 included); MIDI input, freeze/offline bounce, automation write/read, DAW stop/seek/loop, duplicate instances, save/close/open, UI scale, exception/empty-state handling.
6. Release manifest (version, compiler/toolchain, SDK, signed output SHA-256, signed installer and uninstall trace, content licensing list). Release only by explicit owner approval.

## K. Transition policy for current Rack repository

**DO NOT merge, close or retarget PR #78/#109/#110 without the owner's explicit GitHub instruction.** Record a new ADR and approval comment explaining that #77's Rack+Drums 1.0 scope is **superseded or deferred**, but only when the owner authorizes it. New plugin work is separated behind new targets/branch names, ideally `architecture/standalone-suite` with independent Draft PR. Preserve old source; do not rename/remove core files while old CI depends on them.

Extraction steps:
1. Write dependency map for old `CMakeLists.txt`, `VoxFullOnDrums.c`, Bass/Acid PRs, `EffectsChain`, `Primitives` and `vox-ui`.
2. Freeze reference audio renders and checksum test inputs for Bass/Acid/Drums before refactoring.
3. Wrap the **existing DSP**, not redesign it, in a thin `AudioProcessor` adapter. Only then change DSP, with parity tests for unchanged modes.
4. Confirm JUCE CMake plugin target VST3 identity is unique for each plugin: unique manufacturer/product codes/FUID, install paths, category, UI settings. Do not reuse `com.ultimavox.vstengine` identity.
5. Record old/new parameter meaning and explicit migration policy; no hidden import of Rack state.

## L. Technical risks and decisions

| Risk | Severity | Response |
|---|---|---|
| Commercial JUCE licence not selected | Release blocker | Verify legal terms and purchasing decision early |
| Extracting DSP changes sonic output | P1 | Golden render/parity CI before refactor |
| Three plugins duplicate common static code and support cost | P2 | Shared source, one design system, per-product acceptance |
| 96k/64 and heavy layering | P1 | Bench before feature flag acceptance; voice/load budgets |
| Asset licensing or signature provenance | P0 release | Manifest/licence check, no unverifiable included samples |
| Independent-plugin project restore | P0 release | Freeze parameter IDs, state schemas and DAW round-trip tests |
| Multiple formats launched prematurely | P2 | Windows VST3 first; add formats only after target SDK, hardware and CI |
