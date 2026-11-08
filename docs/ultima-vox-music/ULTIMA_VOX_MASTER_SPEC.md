# ULTIMA VOX MUSIC — COMPLETE MASTER DEVELOPMENT SPECIFICATION

**This compendium includes all project files as of 2026-11-08. For independent changes/reviews prefer the individual files.**



---

<!-- BEGIN 00_README_AND_OWNER_DECISIONS.md -->

# ULTIMA VOX MUSIC — Independent Plugin Suite

**Проектная документация / architecture and product definition, revision 1.0 — 08.11.2026**  
**Статус:** выбранное владельцем продуктовое направление, техническая спецификация для независимого ревью; не утверждение о реализованных возможностях.  
**Исходная кодовая база:** [`ultima-vox/vox-electronic-engine`](https://github.com/ultima-vox/vox-electronic-engine), интеграционный снимок `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`; документационный PR [#110](https://github.com/ultima-vox/vox-electronic-engine/pull/110), HEAD на момент аудита `60929c206bd2b8d5cd8789cbcfa7fdfa17ee7059`.  
**Важное различие:** «Уже есть в репозитории» ≠ «доказано production-ready». Все количества движков, фильтров, осцилляторов, функций и тестов ниже, если не помечены как факт исходников, — **целевые требования**.

## 1. Решение

Проектируем **три самостоятельных нативных музыкальных плагина**, а не один универсальный Rack:

| Продукт | Тип | Музыкальное назначение | Первичный формат |
|---|---|---|---|
| **VOX Trance Synthesis** | Virtual Instrument | Bass / Acid 303 / Lead / Pluck / Keys / Pad / Atmos, генерация тембра и MIDI-партий | Windows x64 VST3 |
| **VOX Drums** | Virtual Instrument | 16-пэдовая драм-машина: ударный синтез + sample/layer + генерация грувов | Windows x64 VST3 |
| **VOX FX** | Virtual Instrument | Самостоятельная генерация riser/downlifter/impact/whoosh/psy/texture/glitch FX | Windows x64 VST3 |

Все три работают как обычные VST3-инструменты в Cubase/REAPER и **не требуют загрузки друг в друга**. Внутренняя библиотека `vox_dsp` и `vox_ui` используется совместно при сборке, но каждый плагин содержит нужный код самостоятельно: внешней DLL с общим ABI между плагинами не требуется.

**Уточнение:** VOX FX в этом проекте — MIDI-triggered generator. Он не принимает аудио с другой DAW-дорожки. Если позднее понадобится обработка входящего звука, это отдельный **VOX FX Processor** (effect VST3) с отдельными I/O и приемкой; не следует выдавать его за функцию VOX FX Instrument.

## 2. Почему это предпочтительнее Rack для нынешней задачи

- У каждого плагина свой `AudioProcessor` и `AudioProcessorEditor`; не требуются расширения общего `.vins` ABI для редактирования кривых, пресетов и телеметрии.
- Параметры автоматизации определяются потребностями конкретного продукта, а не универсальным пулом 128×16.
- Каждый продукт получает собственный граф DSP, voice budget и собственные benchmark gates.
- Проще обрабатывать DAW project recall, собственный GUI, MIDI, пресеты, ручное тестирование и поддержку пользователей.
- Убирается стоимость маршрутизации 16 слотов/общих FX/динамической загрузки из **критического пути новых продуктов**. Это не обещает автоматического ускорения DSP: алгоритмы и качество кода надо измерять отдельно.

**Издержки:** три идентичности VST3, три пакета/установщика, три набора host-acceptance и нагрузка от нескольких экземпляров в одном проекте. Потребуется единая библиотека и дисциплина совместимости.

## 3. Техническое решение по стеку

**Предпочтительно:** существующий **JUCE 9 + C++20 + CMake**, JUCE как оболочка/GUI/host integration, собственный независимый от JUCE DSP Core. `JUCE` не навязывает стандартную графику: `LookAndFeel`, пользовательские `Component` и собственные `paint()` позволяют полностью нарисовать (в смысле реализовать в коде) фирменные controls без дефолтного визуального стиля. Графические макеты и изображения здесь **не создаются**.

**Альтернативы:** iPlug2 — серьёзный запасной вариант с permissive-лицензией, но означал бы замену существующего JUCE UI/host-кода. Steinberg VST3 SDK напрямую — больше хостового и GUI-кода. Переписывать VST3/DAW-интеграцию с нуля не рекомендуется.

**Лицензии для проверки перед релизом:** JUCE 9 — коммерческая лицензия либо AGPLv3; VST3 SDK 3.8 — MIT; iPlug2 — zlib-like. AAX требует отдельной процедуры Avid; Apple AU — macOS toolchain, signing/notarization. Источники перечислены в `01_PLATFORM_ARCHITECTURE.md`.

## 4. Порядок реализации

1. `FOUNDATION`: сохранить старую ветку, согласовать новую независимую архитектуру; извлечь существующий DSP/UI в **чётко изолированные библиотеки без переработки алгоритмов**, закрепить Golden Render.
2. `VOX Drums`: завершить первый независимый рабочий VST3, используя уже проверенный код барабанов, образцы, sample-handoff и тесты; расширять до 16 пэдов только после корректного переноса существующих восьми.
3. `VOX Trance Synthesis`: прежде всего работоспособные Bass и Acid, затем Lead/Pluck/Pad/Atmos и генератор партий.
4. `VOX FX`: после независимого FX DSP POC и утверждения параметров — 12 семейств FX, 4 слоя, timeline/motion editor, полноценные наборы контента.
5. Общая система пресетов и UI с отдельной DAW acceptance для каждого продукта.

Это рекомендуемая очередность принятия релизов, не оценка длительности. Архитектуру и UI можно разрабатывать параллельно, но **не требуется ждать нового Rack/ABI**.

## 5. Что не делать автоматически

- Не закрывать, не переписывать и не мержить [#77](https://github.com/ultima-vox/vox-electronic-engine/issues/77), [#78](https://github.com/ultima-vox/vox-electronic-engine/pull/78), [#109](https://github.com/ultima-vox/vox-electronic-engine/pull/109) или [#110](https://github.com/ultima-vox/vox-electronic-engine/pull/110) только на основании этого документа. Прежний release gate формально остаётся открытым до отдельного решения владельца.
- Не использовать `fx/base` и V2.5 как новую основу трех плагинов. #110 — полезный аудит ограничений старой модели, но **направление «расширять Rack ABI ради FX» больше не является предлагаемым**.
- Не менять существующие plugin FUID, IDs, schema и файлы сохранённых проектов. Новым самостоятельным VST3 нужны **новые уникальные идентификаторы**; конкретные 16-байтовые FUID фиксировать в review до первого релиза.
- Не предполагать, что старые Rack-пресеты можно автоматически открывать в новых самостоятельных плагинах. Нужна отдельная версия импортера/конвертера и тесты; для первого релиза перенос не обязателен.
- Не использовать сторонние factory samples, коммерческие пресеты, изображения, шрифты или исходники конкурентов без проверки права распространения.

## 6. Комплект документов

- `01_PLATFORM_ARCHITECTURE.md` — архитектура, стек, threading, state, builds, licences, CI, переход от Rack.
- `02_VOX_TRANCE_SYNTHESIS_SPEC.md` — DSP и музыкальная спецификация синтезатора.
- `03_VOX_DRUMS_SPEC.md` — DSP и музыкальная спецификация драм-машины.
- `04_VOX_FX_SPEC.md` — DSP и музыкальная спецификация FX-генератора.
- `05_ULTIMA_VOX_UI_JUCE_SKIN.md` — полный дизайн-системный и программный план кастомизации JUCE.
- `06_CLAUDE_EXECUTION_BRIEF.md` — англоязычное мастер-задание Claude с этапами и критериями приемки.
- `07_REQUIREMENTS_TRACEABILITY.csv` — матрица требований, реализуемости и тестов.

- `08_LEGACY_RACK_ARCHIVE_AND_REUSE.md` — безопасное архивирование старой ветки, таблица повторного использования существующего кода и тестов.

## 7. Решения, которые нужно подтвердить до изменения кода

- **Продуктовая стратегия уже выбрана владельцем:** три независимых VST3; старая Rack-ветка — исторический архив/донор. До Git-операций оформить ADR, проверить HEAD и план архивации. **По умолчанию: не трогать существующие PR/ветки без отдельного подтверждения конкретных операций.**
- Названия продуктов (предлагаются точные строки выше) и канонические FUID/Bundle IDs.
- Лицензия JUCE, предполагаемые условия коммерческого распространения.
- Список стартовых factory kits/patches/FX и источник лицензированных сэмплов.
- По умолчанию: **Windows 10–11 x64 / VST3 / Cubase 12–15 / REAPER**, затем macOS AU/VST3, CLAP/standalone, AAX — по отдельному решению и реальным тестам.

Все остальные музыкальные и технические характеристики описаны ниже как рабочие проектные предложения, на которых можно готовить Draft PR и принимать решения итерационно.


<!-- END 00_README_AND_OWNER_DECISIONS.md -->


---

<!-- BEGIN 01_PLATFORM_ARCHITECTURE.md -->

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


<!-- END 01_PLATFORM_ARCHITECTURE.md -->


---

<!-- BEGIN 02_VOX_TRANCE_SYNTHESIS_SPEC.md -->

# 02 — VOX Trance Synthesis: инструментальная и DSP-спецификация

**Статус:** проект v1.0, не подтверждение реализации. **Платформа первой поставки:** Windows x64 VST3; C++20 DSP без JUCE, JUCE 9 для VST3/UI. **Музыкальная задача:** один полноценный синтезатор для Psytrance / Full-on / Goa / Progressive / Techno / Trance с проверяемыми характерными тембрами, встроенным генератором партий и обычным DAW MIDI.

## 1. Продукт и принцип исполнения

Один **VOX Trance Synthesis.vst3** (instrument), один уникальный VST3 class ID и manufacturer code, которые фиксируются перед первой публичной сборкой. **Не Rack, не оболочка с загружаемыми VST, не набор несовместимых инструментов.** В интерфейсе звуковые страницы `BASS`, `ACID`, `SYNTH`, `SEQUENCE`, `MOD`, `FX`, `PRESETS`, `ADVANCED`; это редакторы одного движка, а не отдельные VST.

Две независимые синтезаторные части A/B с зоны/velocity/слои, общая секция FX. У каждой части выбирается один из **трёх адаптеров движка**: `VA/HYBRID`, `PSY BASS`, `ACID 303`. Алгоритмы переключаются не в середине голоса: только подготовленная конфигурация/следующая нота либо безопасный crossfade. Допустимы комбинации Bass+Lead, Acid+Pad, 2×Lead; отдельная DAW-инстанция предпочтительнее избыточного мультитембрального режима. Phase 1 имеет один MIDI-приёмник, без внутреннего плагин-хостинга.

**Типы звуков:** тёплый и плотный offbeat psybass, короткий rolling bass, acid резонансные фразы со слайдами и акцентом, supersaw lead, сдержанный Goa lead, FM/hollow lead, plucks, bells/electric keys, dub/trance stabs, широкие pads, evolving atmos, hypnotic arps. Название пресета не заменяет измеримое звучание.

## 2. Сигнальная схема (план)

```text
MIDI + Host transport ─> note owner/voice allocator ─> Part A ─┐
                                   │          │ oscillators    │
                                   │          │ mixer/noise    │
                                   │          │ filter 1/2     ├─> part mixer/pan ─> EQ/drive ─> chorus/phaser ─> delay ─> reverb ─> master
                                   └─────────> Part B ────────┘                           └─> optional 2 FX sends
Global clock/seed -> pattern generator / arp -> SAME MIDI scheduler -> voices
Host automation/base ────────────────> smoothing + modulation ─> DSP (never overwrites host base)
```

No program allocation/loader/sample decode in the callback. Shared reverb and delays are per-instance, not per voice. Aux output and DAW external sidechain are out of first scope.

## 3. Целевой DSP-пакет

| Узел | Количество / режим | Конкретное требование |
|---|---|---|
| Части | **2 A/B** | Mode `VA/HYBRID`, `PSY BASS`, `ACID 303`; independent level, key range, transpose; engine-specific panel |
| VA oscillators | **3/voice/part** | OSC1/OSC2: bandlimited saw/pulse/triangle/sine/wavetable; OSC3: sine/triangle/saw/FM source. Phase/reset/detune/pan/gain; hard sync/ring/FM only when tested |
| Additional sources | **1 sub + 1 noise per voice** | sub sine/square −1/−2 octaves; filtered white/pink noise; high-pass noise in snare-like transient patches |
| Wavetable | **64–256 frames/table (target content)** | interpolation/crossfades, anti-alias via mipmaps; content prepared off audio; fixed per-preset memory budget documented after prototype |
| Unison | **1–4 voices per oscillator** initially | Per-osc phase/width/detune, stereo spread; explicitly count CPU multiplied by active voices. Increase to 8 only with measurements |
| Polyphony | **16 combined notes default, configurable up to 32** | voice budget counts musical voices; note stack; deterministic stealing with short fade; bass/acid modes force mono per active part |
| Filters | **2 per VA voice** | serial / parallel; LP12/24, HP12/24, BP12, notch; resonance, drive, keytracking. Ladder/acid filter separate carefully tested algorithm |
| Filter parameter smoothing | continuous | exponential Hz mapping, resonance bounded, no unstable coefficient jumps, no NaN/Inf near Nyquist |
| Envelopes | **4 per part** | AMP ADSR, FILTER ADSR, MOD1/2 ADSR; curve/slope control, retrigger/free modes, gate/release tested |
| LFO | **4 per part** | sine, triangle, saw up/down, square, sample-and-hold, smooth random; tempo-sync/free Hz; retrigger/global phase |
| Mod matrix | **32 routes per part + 8 global macros** | destination typed/range bounded; per route bipolar amount, curve/smoothing; no modulation→host base writes; cyclic modulation rejected or explicitly ordered |
| Arpeggiator | **1 globally + optional per-part enable** | Up/down/up-down/random/order/chord; 1/1–1/64, triplets, swing, accent, octave, gate, ratchet; release/Stop correct |
| Sequencer | **2 lanes × up to 64 steps × up to 4 bars** | per note pitch, gate, tie, slide (when supported), velocity, probability, ratchet, microshift; stored snapshots |
| FX | **4 serial post-mix slots plus 2 optional sends** | distortion/waveshaper, chorus/flanger/phaser, tempo delay, reverb; true wet/dry and tail policy; if four-slot budget not met, state explicit not fake |
| Oversampling | selectively 2×/4× | nonlinear stages only; quality switch prepared off-RT, correct reported latency; not globally always-on |
| Output | stereo | DC blocker, optional true bypass protection/limiter, controllable trim; no mandatory hard limiter that hides defects |

**Acid adapter:** 1 principal oscillator saw or square + optional sub (not a blanket 3-osc path), monophonic note priority, legato/glide, accent envelope, decay, resonance and filter sweep, 303-like 18 dB nonlinear resonant response as an engineering target, not a claim of exact TB-303 component modeling. Track NoteOn and NoteOff precisely; stop/seek/panic cannot leave a held voice. Slides/ties share a single event scheduler, not a second speculative playback simulation.

**Psy Bass adapter:** transfer/revalidate tested `libs/bass/FullOnBass.*` and `PsyBassVoice.*` from #52/#70. Goal: phase-coherent note-on attack, transient/click+body/sub balance, tune/pitch decay, envelope, saturation, discrete short note events, correct NoteOff/release; avoid permanent beep caused by gates not terminating. Original module render should pass snapshot parity before any timbral redesign.

**VA Synth adapter:** not a feature list without DSP. Require demonstrable saw lead at high notes without gross aliasing, stable resonant sweeps under automation, pluck with fast transient and decay, keys with velocity, evolving pad with controlled stereo correlation. Waveform code reuses and extends `libs/dspcore/Primitives.h` after audited extraction.

## 4. Musical generation and deterministic playback

**Sound Generate** uses seeded structured profiles, not AI: style (Full-on, Goa, Progressive, Classic Trance, Techno), role (Bass, Acid, Lead, Pluck, Pad, Keys, Atmos), energy, brightness, complexity, density, seed, locked parameters. It generates a complete *audible* patch, including oscillator model/mix, filter, envelopes, mod matrix, FX and output compensation. `Mutate` changes unlocked parameters only; `A/B` retains both snapshots. Reject invalid parameter ranges. Fixed seed + engine version + factory content hash ⇒ reproducible result.

**Pattern Generate** uses same scheduling path for preview and MIDI export: style, scale/key/mode, root, phrase length, chord progression optional, density, octave, rests, octave jumps, swing, tie/slide/legato, accent, ratchet, call-and-response, controlled pitch contours. Styles must have rules: Psybass offbeat 1/16 or 1/8 with clear kick space; Acid patterns combine accent/slide/rest; leads use motifs/variation rather than uniformly random notes; pads use long chord tones with voice-leading. Notes outside declared pitch, velocity/gate and maximum events are rejected. Export SMF type 0/1 as explicitly selected; channel, NoteOn/Off and tick positions match **the same scheduling source** used by internal audition. Document deterministic probability semantics: one seeded result per render pass / pattern generation, not free random every block.

**Patch factory acceptance (minimum target):** 48 complete curated presets: Bass ≥12, Acid ≥8, Lead ≥10, Pluck/Keys ≥8, Pad/Atmos ≥10; at least 20 generated/hand-reviewed MIDI phrases covering styles. These are target deliverables, not existing facts. Every factory preset has an audio render, an explicit gain check and a metadata/content license record.

## 5. MIDI semantics

- One-note and overlapping-note ownership, channel 1–16 or Omni with well-documented filtering; MIDI note number, pitch bend, velocity, CC1, CC64, CC120, CC123, Program Change only if specified; no reinterpretation of CC as transport.
- Zero-velocity NoteOn is NoteOff; repeated same pitch with overlapping note IDs uses deterministic stacks; retrigger doesn't orphan old releases.
- Legato and slides render on intended samples; no audio gaps/continuous beep. NoteOff is honored for Gate; One-shot controls explicitly differ.
- DAW Stop / seek / loop policies configurable but safe: stop all current sequencers, clear stuck ownership and tails according to documented Panic vs graceful stop semantics.
- All outgoing MIDI (if supported) uses host capabilities; export-to-file always via controller thread, not `processBlock`.

## 6. Parameter / patch / preset contracts

**Automatable host parameters** target ~130–190 for complete product, to be finalized in one canonical `Parameters.cpp` with stable IDs, min/max/unit/default/normalization, no fixed Rack 128 cap. Do not expose per-step data, wavetable arrays, modulation routes as hundreds of poorly named flat DAW parameters. Store structural patch data in typed/versioned state; preserve compatibility through field IDs + explicit schema migrations. All smoothable host controls exposed through APVTS/parameter attachments; host base changes never silently rewrite other bases. Filter type or engine mode change requires non-RT prepare and safe swap, not dynamic callback allocations.

`getStateInformation` snapshot: schema, product identity, engine state A/B, pattern data, macro routes, FX state, sample/wavetable reference manifests, generator seed and normalized base values. `setStateInformation`: parse/validate/prepare/commit off audio; malformed/newer state retained or rejected with diagnostic; no partial restore. Old Rack projects **not** silently compatible.

Factory bank separate from user bank (`%APPDATA%/Ultima VOX Music/VOX Trance Synthesis/` is a proposed path to confirm at implementation, not existing path). Pack hash/license, version and missing-file states. User program import/export off audio; never load from filesystem in callback.

## 7. UI information architecture (without creating drawings)

- **Performance:** patch name/category, A/B blend, 8 macros, keyboard, live voice/meter and meaningful synth mode, seed/dice/lock controls.
- **BASS:** kick-gap aligned bass gate, oscillator/waveform, amp and pitch curves, phase reset, sub/click, note length/ADSR, velocity and saturation.
- **ACID:** waveform, cutoff/resonance/drive, filter envelope, accent/slide graph, mono/legato, phrase editor. No generic UI substituted for real accent semantics.
- **SYNTH:** A/B sources, 3 osc + sub/noise, mixer, two filters, ADSR and mod depth. Each editor section appears based on active engine capabilities.
- **SEQUENCE:** piano-roll with true MIDI data, 4-bar transport, probability/velocity/accent/tie; drag MIDI with export-parity test.
- **MOD:** 4 LFOs/part, routing table with source/destination/audio range; modulation overlays on knobs separate from host base.
- **FX:** four actual effect slots, chain routing, tail indicator, send/return gains where implemented.
- **PRESETS:** search/category/favorites/User/Factory, compare A/B, save, import/export, reliable undo/redo.
- **ADVANCED:** voice count, CPU/memory, oversample quality, MIDI diagnostics, tuning, plugin/version/hash, realtime status.

Use shared `vox::ui` LookAndFeel and actual widgets; theme specification in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`. No fake waveforms, static meters, dummy buttons or disconnected controls.

## 8. Release acceptance / measurable tests

1. **DSP golden render:** per engine, per family, every osc/filter mode and key edge, 44.1/48/96 kHz and 32/64/128/512 blocks; compare frequency/cycle metrics and acceptable numerical drift, not raw bit-identical across CPU/compilers.
2. **Sound quality:** transient sample alignment, pitch stability in cents measured, filter stability with 100% resonance, spectral alias checks at high fundamental frequencies, no unexplained DC, no clipped default patches; subjective blind A/B for Psy bass and Acid against approved internal references (not copyrighted reproduction).
3. **Host:** Cubase 12–15 / REAPER test NOTE ON/OFF, sustain, CC120/123, transport Seek/Loop, bouncing, preset automation, offline vs realtime match; reopened projects restore exact patch and pattern. Includes baseline VST3 Validator and pluginval where applicable.
4. **Realtime:** zero malloc/new/delete/lock/IO/log in callback by instrumentation; no event drop under documented bounded capacity; CPU on selected owner DAW CPU, p50/p95/p99/p99.9/max, calibrated 96k/64 and other scenarios; multinstance test with Drums+FX.
5. **Preset completeness:** 48 different audible factory patches; 20 usable phrases; A/B comparison, stable seed, locked parameters unchanged by Generate/Mutate.
6. **UI:** all visible controls have data→DSP path, DAW automation tests, logical 1180×760 /1500×920 and Windows 100/125/150/200% DPI, focus/keyboard and mouse gesture; owner visual signoff.

## 9. Dependency-ordered implementation PRs

`SYN-00` audited extraction and golden reference for Bass/Acid; `SYN-01` independent VST3 shell + IDs/state/automation + instrument smoke; `SYN-02` Bass DSP; `SYN-03` Acid DSP; `SYN-04` VA hybrid and filters; `SYN-05` modulation/macros/FX; `SYN-06` musical generator, sequencer and MIDI export; `SYN-07` complete VOX UI and factory bank; `SYN-08` Windows acceptance/security/perf/install. Each PR separately reviewed, no cross-merge into retired Rack.

**Owner decisions:** final plugin identity; minimum factory content quality; whether phase-1 A/B can mix two Acid modes (default: yes, bounded); oversampling modes after CPU measurements. Counts are proposed specification, not proven CPU budget.


<!-- END 02_VOX_TRANCE_SYNTHESIS_SPEC.md -->


---

<!-- BEGIN 03_VOX_DRUMS_SPEC.md -->

# 03 — VOX Drums: самостоятельная драм-машина

**Статус:** целевая спецификация самостоятельного VST3, не характеристика текущего `.vins`. **Цель:** один полноценный жанрово-нейтральный инструмент с профессиональным синтезом/сэмплированием ударных и быстрым генератором грувов. Первый формат — Windows x64 VST3, C++20/JUCE 9.

## 1. Продуктовая концепция

**VOX Drums** запускается как отдельный инструмент, без Rack, без отдельного «хоста пэдов», без вложенной загрузки VST. Зафиксировать один уникальный plugin ID/FUID (не старый `com.ultimavox.fullon-drums`) при создании процессора. Стартовая цель — **16 полностью рабочих пэдов**, совместимых с GM-ориентированной раскладкой по умолчанию, но с переназначаемыми MIDI-нотами. Пользователь может создать любой kit: psy/full-on, Goa, house, techno, trance, hip-hop/trap, breakbeat, cinematic. Жанр — набор алгоритмов, пресетов/паттернов и звуков, **не разная программная архитектура**.

Сохранить и перепроверить **восемь существующих пэдов** из `libs/drums/VoxFullOnDrums.c` (PR #72/#94/#95–#104) как опорную sound-parity базу. Текущие 8 пэдов/16 голосов — факт кодовой базы, новые 16/64 — **требование будущего инструмента**. Приоритет: 8 пэдов без потерь → 16 законченных пэдов → генератор и редактор → acceptance. Не объявлять полные 16 до тестов всех 16.

## 2. DSP сигнал и слои

```text
MIDI / internal step sequencer
   → Note/Choke/Voice ownership → Pad[1..16]
         Pad synth voice (selected model, 2 tonal generators + noise/transient)
         Sample A/B (velocity switching or layer, tuning and bounded interpolation)
         Per-pad chain: envelope → transient → multimode filter → color/saturation → pan
       ├─ MAIN / individual stereo out assignment (predefined Buses)
       ├─ Send 1 (delay)
       └─ Send 2 (reverb)
  → per-pad mixer / mute/solo → stereo MAIN + up to 8 stereo outs
             → shared returns 1/2 → master output and metering
```

Every pad can play **Synth**, **Sample** or **Layer** (Synth+sample A/B), selected per-pad without loading DSP structures in process. Separate FX send values are not duplicated by internal direct routing; establish a single tap-point map and tests for every output. FX tail policy separate from voices; muted input does not necessarily kill already-running shared tails.

## 3. Сколько генераторов и фильтров

| Узел | Целевые параметры | Технический смысл |
|---|---|---|
| Pads | **16**, all real | Note map 0..127, label, color, choke group; full automation of assigned pad levels/essentials |
| Pad synth engine | **8 selectable algorithms/pad** | Kick, Snare, Clap, Hat/Metal, Tom, Rim/Click, Cymbal, General Perc; algorithms available on any pad |
| Oscillators | **до 2 tonal oscillators/pad voice** | Sine / triangle / BLEP saw / metallic partial oscillator as model-appropriate; do not run unused oscillators |
| Noise/excitation | **1 noise source + 1 attack/transient generator/voice** | White/pink/filtered, click/burst; timbre from energy and envelopes |
| Kick model | body sine + separate sub and click | fast pitch envelope (linear/log/exponential), sub amount, click HP/LP, soft clipping, attack/release; precise phase reset |
| Snare | 2 resonant pitched components + noise | spectral body/tuning, snappy, noise decay, transient timing |
| Hats/cymbal | 4–6 inharmonic partials + noise | metallic excitation + HP and short/long metallic decay; enforce alias-safe frequencies |
| Claps | noise + **3–4 microbursts** | burst intervals seed-stable; energy, tone, width |
| Toms | 1–2 osc with pitch envelope | tune/decay/filter saturation |
| Perc | noise + tonal resonator | unusual blips, zaps and textured percussion with bounded FM |
| Sampler | **2 sample players per pad/voice** | stereo/mono WAV, sample start/end, reverse (prepared indexed), loop (optional bounded), pitch resample, sample gain, velocity layering, zero crossfade |
| Per-pad filter | **1 multimode state-variable filter** | LP12/24, HP12/24, BP, off, resonance, tracking; topology select at safe boundary |
| Per-pad envelopes | **3** | AMP, pitch and filter; transient shaper after source, always audible modulation path |
| Humanization | deterministic per-hit | velocity, tiny bounded offset, tune drift and sample alternation, correlated with seed |
| Voice budget | **64 active total max, 8 active per pad** as design target | deterministic steal, voice fade, note lifecycle and choke across pads; benchmark at max |
| FX | **2 shared returns, one post/master processor** | Delay and reverb as send destinations; optional short saturator/compressor; no per-pad reverb buffers |
| Outputs | Main stereo + **8 stereo discrete outputs** | host activation negotiation, unconnected buses safe; no untested unlimited buses |
| Sample memory | budget target configurable at prepare/load | decide max decoded MiB in D-00 after measurement (do not claim 64 MiB arbitrarily); user files loaded off RT, atomically published |

**Synth sound goal by role:** Kick — tight 50–120 Hz fundamental, controlled high click; Psy kick strong fast transient, no unnecessary ringing; house kick deeper and softer; techno kick distortion/tail; snare 180–300 Hz body with broadband snap; hats high-frequency sparkling without overwhelming aliasing; trap hats tiny ratchets; claps layered pulses; toms clean pitch fall; impact perc controlled sub. Frequencies are illustrative timbral targets, not rigid limits or measurements of existing code. Tune control must work continuously throughout supported range without unstable filter state.

## 4. Секвенсор / генератор паттернов

Target one timing engine for playback, MIDI preview and export. **16 lanes × 16/32/64 steps per bar × up to 4 bars** in v1; lane-specific subdivisions and time signature require explicit normalization and test cases. Each step has velocity, accent, probability, ratchet 1–8, microtiming offset, per-step pitch/sample variation where supported, and conditional rules only if verified. Swing is a timing transform in shared scheduler; never regenerate audio independently of MIDI export.

**Generative dimensions:**
- Style: Full-on Psy (four-on-the-floor kick, offbeat bass companion, 16th hats), Goa (rolling and layered percussion), House (classic 4/4 with syncopated open hats), Techno (industrial hats/rim/percussion), Trap (half-time kicks/snares and hi-hat roll subdivisions), Breaks (syncopated kick/snare with swing), Ambient (sparse textures).
- Density 0..1, groove 0..1, humanize amount, syncopation, velocity dynamics, complexity, fill frequency, swing, variation between bars, seed. All knobs have bounded, audible behavior.
- **Generate entire pattern**, **Mutate selected lane**, **Fill last beat/half bar**, **Variation A/B**, **Regenerate locked-safe**, **Euclidean gate** for suitable lanes (k-in-n pattern with rotation), **probability layer** but deterministic under chosen regeneration pass.
- Kick remains intentional when user locks kick lane; snare backbeat rules are genre-specific, not universal. Regeneration must not delete locked cells and must not silently exceed 2048 events or voice bounds; new standalone event budgets are documented after benchmark rather than inheriting Rack internals.
- MIDI export: standard SMF with NoteOn+Off, accurate channels/notes/ticks/gate/velocity/ratchets/microtiming; what is exported equals the canonical triggered event list under same seed/tempo. In Both/Host modes do not double-trigger existing notes; user may explicitly choose Host-only or Internal-only when ambiguity exists.
- Audio drag/export optional after core acceptance: offline bounce as WAV generated by DSP with same seed/patch and tail; rendered from non-realtime job thread, cancellation supported.

## 5. Sample editor and ownership

Support user WAV PCM/float mono/stereo at common sample rates (44.1/48/96 at minimum), robust limits, predictable failure on corrupt files. Sample decode/resample/index/waveform build **off audio**. The GUI may draw waveform from a prepared peak pyramid, not from the audio thread. Controls: start/end, gain, tuning, fine, reverse, fade-in/out, normalize (prepared change), loop (only if reliable), sample A/B velocity split/mix, choke group. Sample reference stores content hash, optional relative path/factory id, missing-asset policy and an explicit Copy into Kit action for portability. Report missing user assets at load; never silently substitute another file.

**Ownership:** Sample pointers remain valid across active voices and program swaps. Use a generation-guarded immutable sample table and deferred retirement off RT (revisit `SampleExchange.*`). No `shared_ptr` destructor freeing audio memory on callback. Test replacing samples while 64 voices, 8 outs, FX tails and transport loops are active.

**Factory content:** target 3 complete and different licensed kits/pattern packs at minimum (Full-on/Psy, House/Techno, Hip-hop/Breaks) plus ≥5 broader styles; target ≥16 pattern presets per core style. Every factory sample must have provenance, author, license, redistribution permission, source hash; if this is absent, render own synthesized sample/audio or omit sample. Do not bundle Freesound audio blindly: inspect each exact file license and attribution/redistribution conditions.

## 6. Параметры и пресеты

Automated parameters: pad level/tune/decay/pan/cutoff/send1/send2, mute where safe (≈8 ×16=128), plus global transport/performance and master/mix parameters (target total **160–240**, final map at D-00). UI-only structural data: sample path, pad algorithm, full note map, routing topology, patterns, sample slice markers, kit categories, waveform cache; store versioned state rather than hundreds of APVTS endpoints. Stable `ParameterID` and type/range across releases; nonlinear curves normalized consistently. User presets: kit, pad-only, pattern-only, all-instrument; independent save/restore and explicit override choices. No hidden collision with old Rack slot parameter IDs.

Host project recall stores all programmed pad values, samples' logical identity, MIDI notes, scenes/patterns, generator seed, FX tails, user path metadata and multibus topology. Reopening works offline with no network. Missing sample must report a deterministic diagnostic and preserve the user’s assignment.

## 7. UI Ultima VOX Music

**Performance:** 4×4 pressure/velocity-sensitive pad matrix, 8 macros, transport indicator, kit browser, live output meters, selected pad indication. No gesture triggers twice.

**SOUND:** per-pad Synth/Sample/Layer, model selector (all 8 models usable), oscillator+noise mixer, filter, amp/pitch/filter envelopes, click/sub, layer velocity, audition/play; editable sample waveform.

**PATTERN:** 16-lane step grid with 16/32/64 steps, bar selector, velocity/probability/ratchet/microtiming editor, swing, duplication, A/B, piano roll if needed, safe undo/redo.

**MIXER:** 16 pad strips with actual level/pan/mute/solo, sends, Main/Out 2–9, return tails and peak meter (polled from DSP). All buttons connected; no visual fake VU meters.

**GENERATE:** style, density, groove, syncopation, fills, humanize, seed, locks; audition + MIDI export.

**ROUTING:** MIDI note/channel map, outputs, choke groups, velocity zones; host bus activation help and diagnostic when DAW doesn't enable outs.

**PRESETS:** Factory/User, kits/pads/pattern categories, compare A/B, no destructive load without undo, content hashes.

**ADVANCED:** sample loading status/missing files, voice statistics, oversampling quality, diagnostics, versions and real CI build metadata.

Default tokens from `libs/vox-ui/Tokens.h`; custom skin implementation in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`.

## 8. Validation / release gate

- Re-render parity of current eight pads at exact reference SHA before improvements. Nine sample-rate/frame combinations; A/B sonic regression with declared tolerances.
- All **16** pads tested via MIDI: each pad sounds, unique routing, note map, multiple voices, choke and velocity, Sample/Synth/Layer, mute/solo, alternate outputs, all send returns.
- Distinct audited reference audio from at least three style families; preset names alone do not count.
- Deterministic generated groove and real MIDI-export parity across stop/loop/seek/reopen, nested probability and ratchet cases; no double hits in Host/Internal/Both.
- Cubase/REAPER: Main and 8 stereo outs, offline bounce same binary, bus activation, project save/reload, automation, sample asset pack/unpack, installer/uninstaller.
- Realtime: 0 audio-thread allocation/lock/IO/decode; bounded voices and sample retirement; NaN/Inf/denorm checks. Do controlled callback benchmarks at 44.1/48/96 kHz and 64/128/512, typical/peak/dense stress, multi-instance and DAW host.
- A green CI does not prove realtime budget. Old Rack Xeon results in #109 remain relevant as warning, **not** a measurement of this standalone engine.
- Visual Gate: 1180×760 / 1500×920 + Windows 100/125/150/200% scaling, readable all 16 pads, no dummy UI; each visible control covered by a binding or functional integration test.

## 9. PR order

`DRM-00` capture old 8-pad golden renders and map ownership; `DRM-01` independent JUCE VST3 shell/state; `DRM-02` port/core integration of 8 pads; `DRM-03` new 16-pad general algorithm/model plus sample/layer; `DRM-04` sequencer & MIDI export; `DRM-05` generator and curated content; `DRM-06` mixer, sends, 8 outs, voice/tail; `DRM-07` complete custom UI; `DRM-08` Windows exact-SHA CI, musical sound review, host installation/acceptance and release package.

**Open design decisions:** final drum note map/16-pad labels, first bundle's legally distributable samples, decoded memory budget, pad FX CPU limits, multibus policy for hosts supporting fewer enabled outputs. None should be silently chosen at coding time.


<!-- END 03_VOX_DRUMS_SPEC.md -->


---

<!-- BEGIN 04_VOX_FX_SPEC.md -->

# 04 — VOX FX: самостоятельный генератор музыкальных звуковых эффектов

**Статус:** будущий standalone-instrument VST3, проектная спецификация. **Не Rack `.vins`** и не обработчик входного аудио. Phase 1 Windows x64, C++20 DSP / JUCE 9 wrapper. Первичная референсная документация: `VOX_FX_Claude_Production_Task.md` (локальное ТЗ владельца), [PR #110 FX-00](https://github.com/ultima-vox/vox-electronic-engine/pull/110), HEAD `60929c206bd2b8d5cd8789cbcfa7fdfa17ee7059`. При новой стратегии **не реализовывать V2.5, Rack-owned Program, `fx/base` или 128-slot pool**. Из аудита #110 использовать только подтверждённые DSP, UI и sound-design требования.

## 1. Для чего нужен инструмент

ВОX FX исполняет **по MIDI-ноте или внутреннему триггеру** готовое по времени событие: riser, downlifter, impact, whoosh, zap, chirp/squelch, атмосферный дрон и т.п. Одна нота может запустить сложное четырёхслойное событие с независимыми траекториями тона, фильтра, громкости, панорамы, эффектами и хвостами. Без сетей и нейросетей. Пользователь должен получить воспроизводимый результат из выбора категории, длительности и кнопки `GENERATE`, затем редактировать каждый элемент.

**Инструмент 1.0** принимает MIDI и использует встроенные осцилляторы/шум/переданные пользователем сэмплы. **Обработка входящего аудио другой дорожки DAW** — не часть этой версии. В перспективе отдельный `VOX FX Processor.vst3` с аудиовходом, иным VST3 ID, отдельной политикой sidechain/bypass/latency, может переиспользовать эффекты, но не должен превращаться в скрытый второй продукт.

## 2. Схема звука

```text
MIDI NoteOn / Generated Trigger
    → bounded event allocator (max 8 overlapping events per instance)
    → event timeline 50 ms .. 32 s, seconds/bars, DAW transport
    ├─ Layer A: sound family 01..12 → amp/pitch/cutoff/pan curves ─┐
    ├─ Layer B: sound family 01..12 → amp/pitch/cutoff/pan curves ─┤
    ├─ Layer C: sound family 01..12 → amp/pitch/cutoff/pan curves ─┤→ stereo sum
    └─ Layer D: sound family 01..12 → amp/pitch/cutoff/pan curves ─┘   │
                                     macro + LFO + modulation matrix ──┤
                          → dry / 4-slot shared FX chain → master safe output
                                      → real diagnostic snapshots → custom UI
```

Fixed bounds: 4 layers/event, max 8 simultaneously active events, up to **64 global granular voices**, shared buffers and FX tails. Thus worst case 32 active layer instances (some families consume multiple oscillators); do not imply 64 grains per event. Allocators and DSP state prepared outside RT. Voice stealing oldest/released with configurable bounded 2–10 ms fade; provide deterministic order.

## 3. Exact synthesis inventory

| Component | Target count | Behavior |
|---|---|---|
| Layer generators | **4**, selectable family per layer | each layer has own gain/pan/width/pitch, offset/duration and curves |
| Oscillators | **up to 2 melodic bandlimited oscillators per layer** | Sine/Triangle/Saw/Pulse, detune, phase; separate FM carrier/mod where applicable; expensive models limit themselves |
| Sub/source | **1 additional sub and 1 filtered noise source** when family uses it | sub pitch/body for Impact, noise color for Riser/Whoosh; no idle compute |
| Filters | **2 per layer** | SVF LP/HP/BP/notch 12/24 dB, resonance, drive; strict stability at high cutoff |
| Curves | **4 per layer + global 4** = 20 | normalized time 0..1, 2–16 nodes per curve; shapes linear/exponential/smoothstep/hold, duplicate time policy explicit, proper anti-zipper |
| Mod sources | **2 LFO + step envelope + random S&H + triggered ADSR + event phase + velocity + follower of own signal** | free Hz or tempo-sync, seeded random, correct reset/discontinuity semantics |
| Mod matrix | **up to 64 routes**, type-aware | source→destination depth/polarity/range/smoothing; cycles prohibited, exact route processing order |
| Macros | **8 automatable** | multi-destination min/max/curve, edits stored with Program; macro never destroys user DAW automation base values |
| Granular | **64 grains total per instance** | Hann/raised cosine windows, pitch/speed, reverse, random position, stereo cloud, deterministic seed, bounded duration and buffer ownership |
| Modal/resonators | **up to 16 modes** for Metallic | clamped resonant feedback, damping and finite output |
| FX processors | **4 serial post-layer slots** | actual signal processing, true bypass/wet/dry, shared buffers; types: EQ/filter, drive, wavefold, phaser, flanger, chorus, delay, reverb, bitcrush, ring modulation, trance gate |
| Oversampling | **2× default option for nonlinear** (off by default until measured), optional 4× | precomputed coefficients; quality mode prepared off RT, host latency updated |
| Trigger mode | One-shot, Gate, Retrigger, Overlap, Ignore | NoteOff/Panic correct; tail behavior documented |
| Duration | **0.05–32 seconds or bar-quantized** | independent event+FX tail; proposed max active tail 60 s, verified host reporting and memory budget |

**Rate limit is by prepared memory/CPU, not by Rack's historical 1,920,000 samples.** Avoid huge per-event buffers: shared delays/reverb allocated once in `prepareToPlay`; granular slots use bounded decoded samples and pointer-safe tables. Make tails finite; `getTailLengthSeconds` must truthfully reflect supported maximum, and host bounce at Stop must render/terminate according to documented policy. Set cap after measured proof; 60 s is a proposed ceiling not achieved behavior.

## 4. Twelve family-specific algorithms and audible acceptance

| ID | Family | Signal recipe | Required aural distinction |
|---|---|---|---|
| 01 | **Noise Riser** | seeded white/pink/brown/blue noise, envelope + start/end log cutoff, optional tonal oscillator; stereo widening | smooth rising spectral centroid/loudness over 1–8 bars, no DC rumble, controllable transient ending |
| 02 | **Downlifter** | falling BLEP/sine pitch plus falling bandpassed noise, controlled slowing tail | clearly descending tonality and brightness, not simply a reverse sample |
| 03 | **Tonal Sweep / Shepard** | 2 clean oscillators + multi-octave Shepard-like overlap (≤8 bounded partials), gain normalization | smooth sweep without an audible hard octave discontinuity and no excess upper aliasing |
| 04 | **Whoosh / Doppler** | noise/tonal transient, high/low-pass trajectory, small bounded variable delay + pan/width movement | believable pass-by with pre/post movement and repeatable stereo position |
| 05 | **Impact / Boom** | click/exciter + 1 body sine with fast pitch fall + sub oscillator + noisy decay | sub impact supports short/long tails, attack stays present on small speakers, no DC/overload |
| 06 | **Zap / Laser** | exponential frequency chirp, optional FM/ring, drive, short impact noise, oversampling where needed | short clean or harsh chirps with clear pitch trajectory, alias check at high sweep frequencies |
| 07 | **Psy Blip / Squelch** | resonant chirp, comb/filter modulation, fast envelope, optional ratchet | rubbery/acidic chirps with controlled resonance and exact rhythmic offsets |
| 08 | **Metallic / Resonant** | impulse/noise exciter into ≤16 resonant damped modes or combs | bell/clang/space metallic textures with controlled decay and no self-excited runaway |
| 09 | **Texture / Drone** | 2 VA oscillators, colored noise, slow LFO/detune, controlled unison/stereo | slowly evolving atmosphere, frequency balance and continuous phase under automation |
| 10 | **Granular Cloud** | sample source, ≤64 global grains, bounded grain scheduler, window, rate-correct pitch/time | cloudy stutters, atmos/riser textures without clicks at grain boundaries or bad sample retirement |
| 11 | **Glitch / Stutter** | internal preallocated own-output buffer or user sample, quantized slice/hold/retrigger/gate | audible gated repeats and syncopated cuts, deterministic exact-grid behavior, no external audio input claim |
| 12 | **Formant / Alien** | oscillator/noise exciter through 2–4 moving formant/bandpass filters, optional ring/FM | vowel-like talking, morphing or alien motion; distinct spectra without prerecorded speech |

**Musical factory examples:** 1-/2-/4-/8-bar Uplifter (Psy), Deep Reverse Downlifter (Progressive), Goa Laser, Dark Squelch, Trance Snare Rise (synthesized), Stereo Doppler Whoosh, Sub Impact, Metallic Stab, Forest Texture, Granular Stutter, Space Formant Sweep. Each example needs true rendered audio and provenance, not label-only metadata.

## 5. Trigger, timing, patterns, output

- MIDI events carry absolute block offsets; re-note triggers stable event ID. Velocity scales intensity and, by design, optional event duration; note pitch transposes tonal layer and optionally changes recipe within key zone. Zero-velocity NoteOn = NoteOff; CC120/123/Stop/Seek/Panic clear ownership without clicks.
- A NoteOn launched before a block boundary remains phase continuous across blocks; loop seek can restart aligned bar-based segments only under an explicit preset setting.
- Time model: bars based on PPQ/time signature/tempo, seconds based on sample clock; BPM automation produces continuous phase for bar-based events, avoiding reinterpretation of already-played samples. Specify mid-note tempo-change behavior in test.
- Internal `FX PATTERN` may trigger events on 1/4–1/32, triplets, bars 1–4. Probability and variations are seeded; same scheduler drives actual events and MIDI export; note-output has no fictitious host source.
- Optional WAV bounce/drag for generated event: **offline non-callback render** via same DSP and complete event+tail, 24-bit PCM or float WAV, chosen SR, stereo; cancelable with progress/correct sample count. MIDI drag exports trigger notes, not a fictitious representation of the resulting waveform.
- Source sample import decode on worker, content hash; missing files audible fallback only by explicit user choice. Existing old `.vins` sample handoff approach may be studied, not imported wholesale as host dependency.

## 6. Generator design

`Generate` request: `{category, style, intensity, complexity, length (bars or seconds), energy, pitch key/tonality, stereo motion, seed, locks}`. Seed/version/content hash determines deterministic output. Generation returns full 4-layer program with selected families, parameter values, 20 curves, 64-route maximum matrix, four internal effects, macro mapping and optional trigger pattern. No audio-network inference or opaque neural model. `Mutate` requires explicit lock semantics preserving user locked fields (including patterns/samples/routing), bounded change; `Variation A/B` separate full program snapshots. Previews use same engine, no fake waveform.

**Factory deliverable target:** ≥48 truly different audible programs across all 12 categories, ≥4 per family; ≥12 musical event patterns. Categories Psy, Goa, Techno, Progressive, Ambient, Cinematic (tags not hard-coded engine variations). Validate silence check, spectral difference, clipping, duration/FX tails, reproducible seed and correct program recall.

## 7. UI Ultima VOX Music

**PERFORM:** category/style browser, selected Program, `GENERATE`, seed/lock, 8 macros, stereo output, real event progress and meters, audition.

**DESIGN:** four vertical layer cards A–D with family selection, active/solo, independent source/amplitude/pitch/filter/pan; embedded thumbnail of **actual evaluated** 4 time curves. Family-specific editor shows only applicable controls, no generic `CHARACTER` whose meaning silently changes under automation.

**TIMELINE:** horizontal duration timeline (0–1 normalized + bars/seconds), 20 envelopes max; draggable nodes, multi-select, snap, curve shapes, zoom, copy/paste, undo/redo; edited curve stored as typed Program not as flat 128 host automation points.

**MOTION:** 2 LFO, one step shape, S&H, envelope follower, typed modulation matrix source/target mapping with real evaluated depth overlays; route cycle policy clear; macro assignment drag/drop plus accessibility alternatives.

**PROCESS:** 4 actual FX stages with precise type/bypass/wet/dry/extra controls, tail view and level gain, non-RT structural switch.

**TRIGGER/PATTERN:** 4-bar trigger grid, Note/Velocity/Probability/Ratchet, host tempo indicator, MIDI output preview, correct interval/transport.

**PRESETS:** Factory/User, search, category, favorites, compare A/B, sample reference status and content import/export.

**ADVANCED:** accurate event/grain count, clip meter, CPU/memory in nonblocking snapshots, quality, MIDI diagnostics, saved state version, end-to-end profiler.

The editor is a normal JUCE `AudioProcessorEditor`, not a `VoxEditorApiV2` module extension. Structural state travels through the plugin's own controller and is immutably published; GUI is JUCE-driven and entirely custom VOX look. Implementation guide in `05_ULTIMA_VOX_UI_JUCE_SKIN.md`.

## 8. Parameters and state

No fixed 128 pool. Create an **explicit stable host automatable map** for audible base parameters, e.g. global 24 + 4 layers × 16 + 4 FX slots × 4 + modulation bases 16 + macro 8 ≈128 (legacy FX-00 map is a convenient starting point, **not** an imposed Rack contract). Keep effect-type and source-family *structural* selectors outside fast automation unless all variants preallocated and crossfade tested. Further FX internals and 20 curves/routes stored in typed/versioned Program snapshot. Program load never silently overwrites an unrelated host automation lane; document preset/automation precedence.

Prepare: oscillator tables, filters/FX memory, grain pools, sample tables; processBlock: bounded no allocations/locks/IO. Program replace: UI builds and validates immutable prepared Program off RT; safe handoff with retirement off RT. Snapshot includes sample hashes/paths, routes, curves, layers, modes, seed, render defaults; `getStateInformation` and `setStateInformation` round-trip with transaction guarantees. A compact binary chunk + schema, explicit endianness bounds and CRC if used, not raw C++ struct dump. User programs portable via manifest and licensed assets.

## 9. Verification / performance / acceptance

- Separate golden renders per family and automation path; measurement of spectral centroid vs time for riser, fundamental descent downlifter, envelope and low-frequency peak for impact, Doppler/panning trajectory, grain discontinuity, formant motion, stereo correlation. Reference should be generated internally (not ripped competitors).
- Test all 12 families × 44.1/48/96 kHz × 64/128/512 blocks and stress multi events/layers; no allocations, NaN/Inf, dropped notes, runaway output, click on retrigger/panic.
- Tempo-change, transport seek/loop/stop, repeated same pitch notes, One-Shot vs Gate, bounce and project recall with sample assets; offline render matches real-time within defined floating-point tolerance.
- CPU on current DAW Windows PC **and** previous Xeon; 4 concurrent FX events, 8 maximum, 64 grains maximum; compare with Drums+Synth in same Cubase session and headroom budget. Record actual p50/p95/p99/max, no invented universal CPU guarantees.
- Every 48 factory preset renders non-silent distinctive audio; preview and saved WAV use real DSP; all editor knobs, curves, modules and meters have verified data path.
- Windows exact-head CI, VST3 validator, pluginval where feasible, Cubase 12–15/REAPER host acceptance, silent install/uninstall, repeatable SHA/binary identity.

## 10. Implementation plan

`FX-00` use PR #110 audit as **read-only input only**, rebase specification to standalone. `FX-01` independent JUCE VST3 shell/IDs/parameters/state and playable first Noise Riser. `FX-02` 4-layer voice engine and timeline with 4 curves/layer. `FX-03` remaining 11 family algorithms. `FX-04` modulation/macro matrix and four true FX slots. `FX-05` sample/granular state, program generator, presets and WAV/MIDI export. `FX-06` completed custom GUI, telemetry and diagnostics. `FX-07` exact-head Windows tests, musical audio review, acceptance/installer and release manifest.

Do **not** merge `#110` (V2.5 Rack extension proposal) as a foundation for these independent VST3s. The old docs' `fx/base` is preserved for history; no need for it in the new project.


<!-- END 04_VOX_FX_SPEC.md -->


---

<!-- BEGIN 05_ULTIMA_VOX_UI_JUCE_SKIN.md -->

# 05 — Ultima VOX Music UI: как полностью заменить внешний вид JUCE, сохранив его функциональность

**Версия:** proposal 1.0, 08.11.2026. **Визуальные макеты/картинки не создаются по этой задаче.** Документ описывает точную реализацию интерфейса и его связывание с DSP в C++/JUCE.

## 1. Главная идея: JUCE — framework, а не внешний вид

JUCE не заставляет использовать стандартный визуальный стиль. `juce::Component` предоставляет события мыши/клавиатуры, иерархию, фокус, перерисовку и bounds; `juce::LookAndFeel_V4` переопределяет отрисовку Slider/Button/ComboBox/PopupMenu; `juce::Graphics`, `juce::Path`, `juce::Drawable` позволяют создавать собственную векторную графику. Данные и хостовые параметры остаются в `juce::AudioProcessor`/`APVTS`. **Никакого HTML/CSS поверх VST3 для первого релиза, отдельного Chromium, Electron/CEF и плагинного браузера.** Не менять SDK ради скина.

Проверенные ссылки JUCE: [LookAndFeel_V4](https://docs.juce.com/master/classjuce_1_1LookAndFeel__V4.html), [Component](https://docs.juce.com/master/classjuce_1_1Component.html), [SliderAttachment](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState_1_1SliderAttachment.html), [ParameterAttachment](https://docs.juce.com/master/classjuce_1_1ParameterAttachment.html), [Drawable](https://docs.juce.com/master/classjuce_1_1Drawable.html). Это **реальные API**, а не придуманная библиотека.

## 2. Что реально можно переиспользовать из старого кода

Репозиторий `ultima-vox/vox-electronic-engine` уже содержит `libs/vox-ui/Tokens.h`, `Typography.h`, `VoxLookAndFeel.*`, компоненты `VoxButton`, `VoxTabBar`, `libs/vox-ui/showcase` и `docs/ui/DESIGN_SYSTEM.md`; подтверждено на интеграционном SHA `f9c8bc6`. **Не переписывать эту библиотеку с нуля**. Сначала откомпилировать её с маленьким независимым `AudioProcessorEditor` и получить скриншоты; не тащить вместе с библиотекой `apps/vst3/PluginProcessor.*` / Rack rail.

**Задача архитектору UI:** превратить `vox::ui` в общую библиотеку, которая зависит **только от JUCE graphics/gui + собственных токенов**, а не от `Rack`, DSP, old APVTS IDs, `InstrumentId` или `.vins`. Три `ProductEditor` используют одну тему и компоненты; различаются содержимым страниц.

## 3. Правила визуального языка

**Дизайн-направление:** техническая музыкальная рабочая станция премиального уровня, строгая геометрия, контраст, высокая плотность управления без «киберпанк-перегрузки». Логотип и идентичность **ULTIMA VOX MUSIC**, индивидуальные имена продуктов. Не копировать skin Serum/Vital/Phase Plant/Kontakt, не использовать чужие asset/bitmap/fonts без лицензии.

| Token | Значение (проверенный legacy UI) | Использование |
|---|---|---|
| Background | `#06121D` | фон редактора |
| Panel | `#0B1925` | основные группы |
| Raised panel | `#102230` | выделенная текущая зона |
| Graph/control | `#051019` / `#0A1722` | редактирование/трек |
| Border | `#1D3B50` | контуры/разделители |
| Accent | `#00DDF5` | взаимодействие/primary active |
| Accent hover | `#42EEFF` | hover/focus |
| Text primary | `#E9F3FA` | основные значения |
| Text secondary | `#9AB2C5` | подписи |
| Warning/Error | `#E3B341` / `#E4425D` | реальные статусы |

**Акценты продуктов** (новые предложения): Synthesis — Electric Violet `#9668F2` (кроме фокусного cyan), Drums — Copper/Coral `#EF805D`, FX — Aqua `#16CDBA`. Акцент — идентичность, а не единственный носитель статуса. `focus`, active, disabled и error различаются контуром, надписью и доступным цветовым контрастом.

**Typography:** токены из `Typography.h` сохранить; no decorative illegible fonts. Base 12–14 px labels, 16–20 px section headers, 26 px title; числовые значения tabular, единицы `Hz/kHz/ms/s/dB/semitones`; установка/встраивание собственных шрифтов допускается только после юридической проверки. Рекомендуется рендер векторных ручек, пиктограмм и графиков; большие фоны можно кэшировать, но не создавать отдельные PNG для каждого значения параметра.

**Control grammar:** `label + editable value + functional control + visible modulation overlay`. Rotary: 270° arc, base value stroke, separate modulation halo, value with unit, double-click reset to actual default, shift/fine drag, wheel optional with host safety, Ctrl+click direct edit only when supported. Pad: label/note/selected state/velocity/trigger feedback, keyboard and mouse accessible. Meter: actual peak/RMS with 20–30 FPS UI polling; clip hold and reset. No fake oscilloscopes, no decorative sliders wired to wrong parameter.

## 4. Два уровня кастомизации

### Tier A — самый дешёвый/надёжный: JUCE controls, полностью свой LookAndFeel

Используем `juce::Slider`, `juce::ComboBox`, `juce::ToggleButton`, `juce::TextButton`, `juce::PopupMenu` как функциональные controls. Заменяем их drawing через собственный `vox::ui::VoxLookAndFeel : public juce::LookAndFeel_V4` (он уже есть в legacy; расширяем, не дублируем). По API реально можно override `drawRotarySlider`, `drawLinearSlider`, `drawButtonBackground`, `drawToggleButton`, `drawComboBox`, `getComboBoxFont`, `drawTooltip`, `createSliderTextBox`.

**Преимущества:** штатные мышиные/клавиатурные события, double-click reset, host gesture через attachments, popup/focus и множество исправленных edge cases остаются JUCE. Реализовать так **все базовые ручки, переключатели, numeric fields, popup-контролы и панели**.

Проверенный по сигнатурам эскиз C++ (предлагаемый код, не существующий файл):

```cpp
class VoxLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y,
                          int width, int height, float proportion,
                          float startAngle, float endAngle,
                          juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
            static_cast<float>(y), static_cast<float>(width),
            static_cast<float>(height)).reduced(5.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        const auto centre = bounds.getCentre();
        juce::Path track, active;
        const auto from = startAngle;
        const auto to = startAngle + proportion * (endAngle - startAngle);
        track.addCentredArc(centre.x, centre.y, radius, radius,
                            0.0f, from, endAngle, true);
        active.addCentredArc(centre.x, centre.y, radius, radius,
                             0.0f, from, to, true);
        g.setColour(juce::Colour::fromRGB(29, 59, 80));
        g.strokePath(track, juce::PathStrokeType(3.0f));
        g.setColour(slider.isEnabled()
                        ? juce::Colour::fromRGB(0, 221, 245)
                        : juce::Colour::fromRGB(88, 116, 135));
        g.strokePath(active, juce::PathStrokeType(3.0f));
    }
};
```

*Пример демонстрирует переопределение графики: label/value/modulation halos, size policy и exact default reset добавляет существующий `VoxKnob` composition. Перед включением требуется компиляция с закреплённой версией JUCE.*

Привязка обычной ручки без собственной реализации MIDI/host gestures:

```cpp
juce::Slider cutoff;
cutoff.setSliderStyle(juce::Slider::RotaryVerticalDrag);
cutoff.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
// keep attachment alive for exactly the lifetime of the slider and APVTS
juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment {
    processor.apvts, "synth.filter.cutoff", cutoff
};
```

**Важно:** `processor.apvts` в примере — поле **нового проектируемого Processor**, а не утверждение о текущем исходнике. `synth.filter.cutoff` — пример ID, финальная карта утверждается в отдельной спецификации параметров. Для настоящего C++ члена класса порядок конструирования `Slider`/`APVTS`/attachment нужно обеспечить на уровне полей и RAII.

### Tier B — настоящее кастомное редактирование: свой juce::Component

Нужен для **waveform editor, curve/timeline, pattern grid, modulation matrix, piano roll, XY pad, routing graph, sample slices, per-pad transient curves**. Эти редакторы не «присосать поверх» обычного Slider: у них собственное typed state, выделение узлов, Undo/Redo, snap, selection, hit regions, export.

`CustomCurveEditor : public juce::Component` отвечает только за `paint`, `mouseDown/Drag/Up`, keyboard/focus и local selection; **не** за lifetime аудиообъектов. UI получает immutable snapshot из `EditorModel` (контроллер thread), редактирует копию `Program` и публикует validated prepared snapshot через bridge. Во время drag repaint можно локально делать 30–60 FPS, а commits коалесцировать; каждый ввод должен достигнуть DSP предсказуемо и восстанавливаться в DAW проекте. `UndoManager` на controller/message thread.

Для кастомного **односкалярного** параметра вместо Slider применяем `juce::ParameterAttachment` с реальными `sendInitialUpdate`, `beginGesture`, `setValueAsPartOfGesture`, `endGesture`, `setValueAsCompleteGesture` — методы официально документированы. Не вызывать их из `processBlock`.

**Формат связей:**

```text
DAW automation ───> JUCE parameter (stable ID) ──> base atomics ──> DSP smoothing
         ↑                      ↑
         │               ParameterAttachment / APVTS Attachment
VOX custom control <────── message-thread updates

Custom curve / pattern ─> EditorModel ─> validate/prepare snapshot ─> RT-safe publish ─> DSP
                            ↑
                     Undo/Redo + serialized state

DSP peak/voice/grain counters ─> lock-free POD telemetry snapshot ─> UI timer 20–30Hz
```

## 5. Архитектура исходников (предложение)

```text
libs/vox-ui/
  Tokens.h                  // already exists; extend without breaking semantics
  Typography.h              // already exists
  VoxLookAndFeel.*          // already exists; theme entry point
  theme/Theme.h             // theme semantic tokens and per-product accents
  controls/VoxKnob.*        // composed JUCE Slider + label/value/mod overlay
  controls/VoxToggle.*      // JUCE button, custom skin
  controls/VoxSelector.*    // JUCE ComboBox, custom popup
  controls/VoxMeter.*       // real peak/RMS, UI poll, zero audio dependency
  controls/VoxXYPad.*       // 2 parameter attachments with gestures
  editors/VoxCurveEditor.*  // typed snapshot + undo
  editors/VoxStepGrid.*
  editors/VoxWaveform.*
  editors/VoxModMatrix.*
  layout/VoxResponsivePanel.*
  assets/                   // only licensed SVG/PDF/typography data; no fonts disclosed
products/*/Editor.*         // each product's screen composition, not duplicate widgets
```

Do **not** create `legacy Source/UI/`, fork three VoxLookAndFeel copies, or couple `vox-ui` to Rack or DSP. `vox-ui` links `juce_gui_basics` and graphic modules; product layer links DSP only through a narrow `EditorModel`/`ParameterStore` interface. Shared preview renderer should reuse the same DSP curves/math in non-RT mode for accuracy; **no renderer owns mutable voice pointers**.

## 6. Product-specific workspaces (no bitmap mockups)

| Product | Primary navigation | Specialized editors |
|---|---|---|
| **Trance Synthesis** | PERFORM / BASS / ACID / SYNTH / SEQUENCE / MOD / FX / PRESETS / ADVANCED | 3-OSC mixer, dual filters, modulation matrix, piano roll, accent/slide, note automation overlays |
| **Drums** | PERFORM / SOUND / PATTERN / MIXER / GENERATE / ROUTING / PRESETS / ADVANCED | 4×4 pads, model-specific envelopes, waveform with sample slices, 16-lane velocity/ratchet grid, 16 mixer strips |
| **FX** | PERFORM / DESIGN / TIMELINE / MOTION / PROCESS / TRIGGER / PRESETS / ADVANCED | 4 source cards, 20 curves, 64-route modulation, 4 FX stages, playback timeline, real event/grain telemetry |

**Common header:** logo (product-specific), preset selector, tempo/sync state, Save, Compare A/B, Undo/Redo, CPU (measured/nonblocking), output peak. Performance buttons must have true state. **Common footer:** MIDI activity, voice/event count, patch dirty, plugin version, sample/missing file alerts. Dynamic hiding vs dummy controls: if DSP feature absent, section is not exposed as usable.

## 7. DPI, resizing, rendering and performance

- Editor min design target 1040×700; standard 1180×760; large 1500×920; scale 100/125/150/200% Windows. Default minimum is a design hypothesis; verify controls and adjust. Use `juce::FlexBox`/`juce::Grid` or computed `resized()` layout; no hard-coded absolute coordinates for critical controls. Editor resize saves only display dimensions, not DSP state.
- Vector art first; cache static drawing (background, waveform mini maps, spectrogram/oscilloscope decimated buffers) in JUCE Images prepared on the **message thread**. Never instantiate SVG parsers, dynamic fonts, or resource files in audio callbacks. `juce::Drawable::createFromSVGString` / `createFromSVGFile` parse supported SVG features; use static assets not volatile untrusted markup.
- UI meters/graphs: 20–30 fps for routine meters, 30–60 fps during active gesture or detailed scope; if editor hidden, suspend repaint timers. Avoid `juce::OpenGLContext` by default: it adds context/platform complexity, plugin-host interactions and lifecycle hazards. Consider hardware rendering only after profiling actual curve editor CPU; never require GPU for sound.
- Use state/colour not only for accessibility: focus rings, keyboard step, tab order, tooltip/accessible names and alternate text input for all drag-controlled values.
- All controls support `enabled`, `disabled` (with why), `hover`, `pressed`, `focused`, `active`, `warning`, `error`. Tooltips never substitute for functional labels.
- Handle dark-mode? VOX theme remains dark 1.0; additional skins can be data-only theme variants later with deterministic token changes. Do not let theme change VST3 automation IDs.

## 8. Visual + functional acceptance (hard release gates)

| Gate | Required proof |
|---|---|
| Skin | No default JUCE buttons, standard blue slider/popup or OS-form visual leaks in the actual editor (including dialogs hosted by plugin where possible) |
| Automation | Moving every exposed knob produces proper begin/change/end gestures, host writes automation and restores it; DAW automation updates control without recursion |
| Structural | Curve nodes, pad mappings, sample slices and modulation routes actually change DSP and survive save/reopen; multi-node undo/redo correct |
| Telemetry | Meter equals actual processed peak/RMS envelope within documented decay; event/playhead/voice counts read real snapshots; 0 UI-origin audio-thread allocations |
| Layout | screenshot-based Visual Gate at 1180×760 and 1500×920, scaling 100/125/150/200%, no invisible/overlapping controls |
| Lifecycle | Editor open/close repeatedly during playback and DAW project load, no UAF; modal/popups don't hang DAW; standalone not required for phase 1 |
| Performance | UI closed vs open measured callback timings; no data locks/blocking calls in callback, bounded repaints, no continuous drawing when hidden |
| Consistency | Same knobs/buttons/tabs, keyboard behavior, typography and spacing across all 3 plugins; different product pages are intentional not divergent skins |

## 9. How Claude should implement the skin (task decomposition)

1. **UI-00:** audit `libs/vox-ui` for hidden Rack dependencies, API surfaces and current verified versions; make a minimal separate test harness/editor that uses old theme without Rack.
2. **UI-01:** finalize shared `Theme`, token color/font/spacing specs and semantic component states; compile documented `LookAndFeel` overrides.
3. **UI-02:** implement `VoxKnob`/`VoxSelector`/`VoxButton`/`VoxToggle`/`VoxMeter` with correct JUCE APVTS and ParameterAttachment binding, toolbar and dialogs.
4. **UI-03:** implement responsive scaffold, header/preset browser/tab/page composition + basic DPI and keyboard acceptance.
5. **UI-04:** add waveform editor, curve timeline, step grid, XY and modulation matrix as genuine typed-state editors with undo/redo and nonblocking DSP publication.
6. **UI-05:** product-specific composition for Drums, Synthesis and FX with **exhaustive control→host parameter/Program→DSP audit matrix**, Visual Gate, real DAW screenshots, UI regression and performance tests.

The existing default JUCE widgets are **not painted over with a disconnected SVG**. The functional widgets stay connected to actual parameters; only drawing and complex editing are custom. This is what makes an original ULTIMA VOX MUSIC UI fast to develop and trustworthy in a DAW.


<!-- END 05_ULTIMA_VOX_UI_JUCE_SKIN.md -->


---

<!-- BEGIN 06_CLAUDE_EXECUTION_BRIEF.md -->

# CLAUDE CODE MASTER TASK — ULTIMA VOX MUSIC: three production standalone instruments

**Date:** 2026-11-08. **Repository:** `ultima-vox/vox-electronic-engine`. **Status:** development specification, NOT an instruction to merge existing PRs or release anything. **Owner pivot:** retire the single Kontakt-style Rack runtime, reuse audited legacy assets only, and create three independent Windows x64 VST3 plugins with a shared library and consistent ULTIMA VOX MUSIC UI.

## 0. Mandatory reading / source of truth

Read this file and every linked document before any implementation:

1. `00_README_AND_OWNER_DECISIONS.md` — product architecture and deferred format scope.
2. `01_PLATFORM_ARCHITECTURE.md` — build, audio runtime, parameter semantics, state, threading, CI, licensing.
3. `02_VOX_TRANCE_SYNTHESIS_SPEC.md` — bass/acid/synth DSP, generators, osc/filter/envelopes.
4. `03_VOX_DRUMS_SPEC.md` — full 16-pad Drum Engine, sampler and pattern generator.
5. `04_VOX_FX_SPEC.md` — 12-family 4-layer FX sound design generator.
6. `05_ULTIMA_VOX_UI_JUCE_SKIN.md` — custom JUCE skin without losing DAW parameter/gesture behavior.
7. `07_REQUIREMENTS_TRACEABILITY.csv` — acceptance map for every delivered feature.
8. `08_LEGACY_RACK_ARCHIVE_AND_REUSE.md` — safe Git archival plan and code-reuse inventory.

Relevant GitHub evidence: old `integration/rack-1.0` observed `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`, PR #52/#70 Bass, #59/#69/#92 Acid, #72/#94 Drums, #78 Rack, #79 `.vins`, #80 pool, #109 performance, #110 FX-00 documentation. **Re-check remote SHA before relying on any of these.** Historical descriptions may not match code or current CI.

## 1. Change of architecture — binding direction

Products:
- **VOX Trance Synthesis** — one VST3 instrument, Bass + Acid + Hybrid VA/Synth engines, two parts, generation of sound patches and MIDI patterns, presets, custom editor.
- **VOX Drums** — one VST3 instrument, complete 16-pad Synth/Sample/Layer machine with 8 sound algorithms/pad, real sequencing and drum-pattern generation, mixer, multi-out and custom editor.
- **VOX FX** — one VST3 instrument generating layered risers, downlifters, impacts, whooshes, zaps, psy squelches, granular/metallic/texture and glitch from MIDI and patterns, **not an incoming audio insert**.

**Do not create a common Rack host.** No nested VST hosting, no `.vins` runtime, no 16-slot graph, no cross-plugin dll dependency, and no V2.5 extension from #110. Each plugin is a self-contained JUCE `AudioProcessor` / `AudioProcessorEditor`; each has a unique immutable VST3 identity/parameter map/state schema. Shared C++20 code is statically linked as `vox_dsp`, `vox_audio_runtime`, `vox_ui`, `vox_preset`, `vox_testkit` where worthwhile; shared architecture is **build-time reuse**, not host infrastructure. Start from the existing CMake/JUCE project and audit dependencies; do not rewrite JUCE from scratch.

Retain DSP algorithms and tests only after source SHA/license verification and before/after golden render. Do not merge #78, #109 or #110 into new `main` or target the current Rack staging; branch implementation proposals on an approved isolated standalone-suite branch. **Do not delete Rack base branches, close PRs, or move tags without first preparing and independently reviewing the archival procedure**. The owner's intent is to preserve old work as a legacy archive; no destructive operation is needed.

## 2. Architecture gates (P0/P1)

### RT and lifecycle, P0
- `processBlock` and anything called from it: no heap alloc/free, no locks/spin waiting, no disk/network/ZIP/WAV decode/logging, no dynamic module loads/host UI calls. Bound worst-case iterations and event queues; instrument allocation probes on the **actual Windows audio thread** and check blind spots.
- Prepare all oscillators/tables/oversampling/delays/FX/sample buffers/voices at prepare or controller handoff. Snapshot publish/retire must not invoke destructors/free on audio thread. Validate class/device/transport state before process; safe Panic, CC120/123, NoteOff, same-note re-trigger, host Stop/seek/loop; no stuck voices.
- For changing sample, structural parameters, sound models or engine types: prepare on non-RT, apply safe snapshot on deterministic boundary with crossfade/reset policy. No unsafe pointer reuse or UI passing mutable buffers into DSP.
- Handle 44.1/48/96 kHz and 32/64/128/512 frames; mono/stereo semantics documented, variable block sizes including 0 if host sends it. Preserve denormal/NaN/Inf protections and gain headroom.

### Host identity, state, automation, P0
- Three **different** VST3 FUIDs/bundle names; **never** reuse Rack IDs `com.ultimavox.vstengine` and `slotNN.pKKK`. Pin actual generated IDs at foundation stage in a documented file, not this prose.
- Every exposed audio parameter has an immutable ID, semantics, range, choice ordering and unit. JUCE APVTS for base automation; modulation is effective value in DSP and does not rewrite DAW lanes. Audio-rate automation claims require an actual tested host transport path; do not promise it by default.
- Versioned `getStateInformation` / `setStateInformation` restore engine patches, typed curves, patterns, samples references, generator seed, FX/tails and UI state separately; malformed/unrecognized state never leaves an accidentally half-loaded plugin. No silent conversion of old Rack projects.
- Real user waveform editors / MIDI drag/preview and timing must share the exact implementation path with rendering/export. Do not duplicate schedulers for UI and sound.

### Product completeness, P1
- A nominally implemented feature is **not complete** if it has no audio impact or lacks save/restore, automation/gesture/host test, error state, or self-hosted Windows build. No fake UI meters, sample waveforms, placeholder 'Generate', static images of unavailable controls, or silent bypass of required features.
- Original factory patches/kits/sample license manifest must ship; ensure reproducible generation and audible distinction across required genres; no copying commercial competitors' proprietary content.
- CPU: report p50/p95/p99/p99.9/max against block deadlines and missed-block counts on **owner-selected current Windows DAW machine and existing self-hosted Windows runners**. Classify runner/service failures as infrastructure, compile errors as compile, and per-block misses as performance. Old #109 P1 is not solved merely by changing architecture.

## 3. Branches, PR governance and extraction

### FOUNDATION (no sound-code rewrite)

`FOUND-00`: current Git audit and archival inventory. Record current main/staging heads, open stacked PRs, dependencies, reusable source SHA, CI and exact test commands. Propose an immutable archive tag and new standalone branch. **Do not perform destructive Git operations.**

`FOUND-01`: minimal monorepo CMake restructure with three empty-but-loadable JUCE VST3 processors with distinct identities, visible empty editor/sound initialization/test, and shared libs; compile and run VST3 Validator. The first PR should have only necessary files and no wholesale checkout of Rack code.

`FOUND-02`: port independent `Primitives.h`, `EffectsChain` and `vox-ui` with dependency boundaries, golden test harness; no unreviewed DSP optimization or sound change.

`FOUND-03`: common sample ownership, transport/MIDI scheduler, versioned preset/Program codec, RT telemetry and shared UI model interfaces, with host-facing integration tests.

**Keep each PR Draft until independent review**. Explain dependency and base/head SHA, pass/fail CTest, Windows runner ID, uploaded render artifacts and exactly what production gate is closed. Never merge without explicit owner authorization.

### VOX DRUMS

`DRM-00` legacy eight-pad render / kit and test corpus. `DRM-01` VST3 processor/params/state + Main. `DRM-02` tested eight-pad source migration, sample lifetime. `DRM-03` general 16-pad/8-model DSP and 2-sample player per pad. `DRM-04` exact MIDI+audio scheduler/exports. `DRM-05` genre generator and legal content. `DRM-06` mixer+send/return+8 stereo outs (real DAW activation). `DRM-07` complete ULTIMA VOX MUSIC skin. `DRM-08` exact-Windows/release acceptance.

### VOX TRANCE SYNTHESIS

`SYN-00` Bass/Acid old source audit and reference renders; do not import unreviewed MIDI lifecycle failures. `SYN-01` standalone VST3 shell/state and routing. `SYN-02` working Psy Bass voice/NoteOff. `SYN-03` working Acid 303 mono/legato/accent/slide. `SYN-04` bandlimited 3-osc VA/hybrid + 2 filters, 2 parts, voice budgeting. `SYN-05` envelopes/LFO/mod/macros/FX. `SYN-06` seeded patch+pattern generator and MIDI export. `SYN-07` full UI/factory patches. `SYN-08` exact-Windows/release acceptance.

### VOX FX

`FX-00` note that PR #110 describes obsolete Rack ABI restrictions; do not implement V2.5. `FX-01` standalone JUCE shell and real Noise Riser. `FX-02` 4-layer bounded event runner/curves. `FX-03` full other 11 families with audible golden renders. `FX-04` modulation/macros/four real FX slots. `FX-05` granular/sample content, generator, import/export, bank. `FX-06` real custom FX editor including 20 curves and telemetry. `FX-07` Windows sound/performance/host acceptance.

### SHARED UI

`UI-00` reuse audit of old `libs/vox-ui`. `UI-01` shared theme and `LookAndFeel` skins; `UI-02` parameters/gesture-bound knob/button/selector/meter; `UI-03` responsive editor framework/100–200% DPI; `UI-04` custom curve/waveform/step/piano/XY/matrix editors; `UI-05` control-to-DSP evidence and screenshots in the **real** plugin editors. No image mockups or visual generation unless owner explicitly instructs.

## 4. Required code/design deliverable in EVERY PR

1. Problem and scope (product/phase) and **exact owner-approved upstream base HEAD**.
2. Changed-file inventory, reusable source SHA, any new dependencies/licenses, explicit `no ABI/host/ID changes` where true.
3. Implementation diagram (data path, ownership, realtime thread handoff), parameter map, state version diff, CPU/memory expectation labelled estimate vs evidence.
4. Tests and **actual result artifacts**; Windows self-hosted workflow/job link; render sample/listening notes; host regressions.
5. Known limitations and which required features are missing; never call an incomplete prototype production-ready.
6. Open review findings P0/P1/P2 with path/line/repro/risk/verifiable acceptance; only resolve with tests/code proof and reviewer acknowledgement.
7. Build/install/uninstall SHA-256, signing/security decisions, support matrix and exact binaries tested.

**P0 acceptance checklist:** all core controls functional; verified RT/no UAF; no stuck MIDI; valid preset save/load; no arbitrary outside-audio DSP free; correct plugin parameter IDs/host automation; executable Windows VST3. **P1:** sound quality/pattern-generation, three different drum kits, 12 FX families, synth Acid/Bass sound acceptance, 48 FX and 48 Synth presets, 16 real drum pads, all custom UI bindings. Performance/DAW must be measured on actual build before release.

## 5. Decision guidance for the agent

When specifications conflict with real code, **do not silently invent an implementation**. Quote exact path, symbol, SHA, why it conflicts, 2–3 options and your recommended minimal solution. Do not inflate a lightweight C++ DSP primitive into a general plugin host. Do not create a new GUI framework. Stop before material schema/identity or security change if not explicitly approved. After verifying framework license suitability, JUCE remains primary; iPlug2 is evaluated only if licensing or a material build requirement blocks JUCE.

The product owner has chosen the **separate instruments direction** and says old Rack should be kept as archive/donor. Implementation proposals may be scoped to that direction, but archival destructive operations, merging and releases still require separate explicit approval. For all files that do not exist yet, document them as *proposed paths*.

## 6. First expected reply from Claude

Before writing product DSP, deliver one **FOUND-00 architecture report** (no major code):
- exact current remote branches and SHAs; new standalone branch proposal and archival tag procedure;
- source-level reuse matrix with test/CI status, resolved/unresolved defects;
- proposed CMake targets with three distinct products and shared libraries;
- actual full host parameter maps, plugin identity freeze strategy and one migration/state policy per product;
- complete PR DAG with first three Draft PR scopes and Windows CI jobs;
- JUCE licensing / dependency policy and ULTIMA VOX MUSIC UI audit;
- a meaningful auditory benchmark/test-fixture inventory and what cannot yet be verified;
- decisions requiring the owner (only those blocking actual work).

Proceed only with reviewable, measured code; no silent merges, no replacing old main, no changing #77/#78 status until owner-directed migration communication.


<!-- END 06_CLAUDE_EXECUTION_BRIEF.md -->


---

<!-- BEGIN 08_LEGACY_RACK_ARCHIVE_AND_REUSE.md -->

# 08 — Безопасное архивирование старого Rack и перенос пригодного кода

**Решение владельца от 08.11.2026:** общий хост/Rack переводится в legacy; будущие продаваемые продукты — три независимых VST3. Старую ветку сохранить как **исторический read-only источник** для кода, ABI/тестов и аудиореференсов. **Не удалять историю и не пытаться автоматически перенести все старые PR.**

## 1. Что означает «в архив» в Git/GitHub

GitHub поддерживает **архивирование целого репозитория**, но не отдельный специальный `archive=true` флаг для ветки. Архивировать весь `ultima-vox/vox-electronic-engine` нельзя, если тот же репозиторий станет монорепозиторием новых продуктов: он перестанет принимать обычные правки. Подход без потери кода:

1. Зафиксировать фактический HEAD старого `integration/rack-1.0` и список незавершённых дочерних PR, workflows, artefacts, внешних зависимостей, подписей и тестов. Исторически проверенный SHA: `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`; **проверить актуальное состояние перед тегом**, а не считать его вечным HEAD.
2. Создать **аннотированный неизменяемый архивный тег** `archive/rack-legacy-2026-11-08` (имя предложено, если не занято) на подтверждённый SHA. При желании дублировать его в отдельной ветке `archive/rack-legacy-2026-11-08`, защищённой от push; это только указатель на сохранённую историю, не переписывание кода.
3. Оставить старые рабочие `integration/rack-1.0`, `fx/base`, `perf/router-channel-index` и другие ветки нетронутыми, пока есть открытые PR, поскольку **удаление base веток делает review/сравнение зависимостей недостоверным**.
4. Отдельным issue/ADR зафиксировать, что #77 legacy release gate **superseded by three-standalone direction**, а #78 и #110 **reference only**. Обсуждения и P1 по performance оставить доступными; старые CI не выдавать за green новых инструментов.
5. По завершении переноса донорского кода (и только после владельческого решения) закрыть неподходящие Draft PR как `superseded`, с ссылкой на архивный тег. Ничего не merge «ради сохранности» — git tag уже сохраняет историю.
6. Новая ветка разработки: `next/standalone-suite` от выбранной согласованной базы; переносить **точечными коммитами**, зафиксировав SHA источника/допустимые диффы/golden parity. Сценарий `merge integration/rack-1.0 into main` не допускается без отдельного архитектурного ревью.

**Контрольный пример команд** (не выполнялись и здесь приведены как процедура, применять после верификации):

```powershell
# Run from a clean local clone. Verify target branch still exists and actual remote SHA.
git fetch origin --prune
git ls-remote origin refs/heads/integration/rack-1.0
git show --no-patch --format=fuller origin/integration/rack-1.0
# Confirm SHA against the reviewed release baseline BEFORE performing write:
git tag -a archive/rack-legacy-2026-11-08 <VERIFIED_SHA> -m "Legacy Vox Rack archival snapshot, standalone pivot"
git push origin refs/tags/archive/rack-legacy-2026-11-08
```

Перед `git tag` заменить `<VERIFIED_SHA>` действительным SHA. Не запускать `git branch -D`, `git push --force`, `git reset --hard` на ценных рабочих ветках. Если tag already exists, **не передвигать его**: выбрать дату/ревизию и написать ADR.

## 2. Аудированный перечень кандидатов на reuse

Пути ниже получены через GitHub PR diff/file inventory и чтение исходников у `integration/rack-1.0` `f9c8bc6`, а также PR #52/#59/#69/#70/#72/#79/#92/#110. Это **кандидаты**, не утверждение о готовности в другом продукте. Перед переносом каждый проходит reference render & dependency audit.

| Категория | Точный путь/источник | Решение | Гейт переноса |
|---|---|---|---|
| DSP BLEP/SVF/ADSR | `libs/dspcore/Primitives.h` | **Reuse** (JUCE-independent) | тест частоты/aliasing, filter stability, no allocations |
| Effects | `libs/effects/EffectsChain.*` | **Reuse with adapter** | снять зависимости от общего FX state/return graph, тестировать tails/latency/RT |
| VOX UI | `libs/vox-ui/`, `docs/ui/DESIGN_SYSTEM.md` | **Reuse core** | вынести зависимость от Rack, сохранить Theme, LookAndFeel, bindings, visual gate |
| Drums | `libs/drums/VoxFullOnDrums.c`, PR #72 + staged #95–#104 | **Extract and extend** | золотые рендеры 8 пэдов, затем переход на 16 с чёткой версией алгоритмов |
| Bass | `libs/bass/FullOnBass.*`, `libs/bass/PsyBassVoice.*`, PR #52/#70 | **Extract** | объективная проверка NoteOff/Release, phase, bass transient, sound parity |
| Acid 303 | `libs/acid/AcidEngine.*`, `AcidGenerator.*`, PR #59/#69/#92 | **Extract, fix reviews** | не переносить unresolved lifecycle/slide/Panic/ABI баги; direct DSP tests |
| Samples | `libs/instrument/samples/SampleDecoder.*`, `SampleLoader.*`, `SampleExchange.*` | **Adapt** | сделать standalone sample store, lifetime/retirement on nonRT, path portability |
| Sequencer/pattern | `libs/pattern/Pattern.*`, `libs/midi/*`, tests | **Selective reuse** | instrument-specific scheduler, one event source for render/export, transport parity |
| FX routing/duck | `libs/rack/AudioRouting.*`, `RackDuckingTests.cpp` | **Reference/tests only** | standalone instruments don't need 16-slot graph; adapt own buses/sends correctly |
| JUCE shell | `apps/vst3/PluginProcessor.*`, `PluginEditor.*` | **Reference only** | new processors/IDs per product, no Rack constructor/loading/16×128 slots |
| `.vins` loader/signatures | `libs/instrument/vins/*`, `native/*`, PR #79 | **Archive; optional idea reuse** | standalone VST3 needs no dynamic instrument loading; don't bring security attack surface |
| FX-00 V2.5 docs | `docs/fx/*`, PR #110 | **Architectural reference only** | standalone programs via processor state, no shared ABI extension |
| Native C ABI | `libs/instrument/abi/VoxModuleAbiV2.h` | **Archive** | no need for VST3 host-owned nested module; local C adapter possible only during transitional tests |
| Fixed automation pool | `libs/rack/RackState*`, `RACK_PARAMETER_CONTRACT.md`, PR #80 | **Do not port as constraint** | each plugin has its own stable IDs and APVTS |
| Realtime/CI | `tests/support/AllocationProbe*`, `tests/performance/*`, `.github/workflows/*` | **Reuse methodology** | change test subject to new exact binary / processor, don't recycle old green result |
| Content signing | `docs/release/RELEASE_SIGNING.md` | **Reference** | `.vins` signing differs from VST3 Authenticode/installer signing; define new policy |

**No blind cherry-pick**: old PRs contain shared Rack UI/host state edits. For portable DSP copy only narrow source and tests after license audit; prefer `git restore --source <SHA> -- libs/bass/...` into a new worktree and explicit test commit, not entire PR merge. `git subtree` is unnecessary until proven.

## 3. Compatibility and release identity

- The three plugins use unique VST3 class identities, plugin filenames and parameter IDs. Do **not** reuse the old VstEngine FUID `com.ultimavox.vstengine` or `slotNN.pKKK` to create false compatibility. Freeze IDs before public release.
- Old `.vins` files do not automatically install into separate plugins, old Rack user projects are not automatically opened. Export/import data converter, if valuable, is a **separate versioned tool** with explicit diagnostics and tests; no silent fallback.
- New factory patches can be generated from source algorithms, but previous render references remain only QA materials. Do not bundle any reference sample without audited redistribute license.
- Product docs/PRs must mention exact source SHA for every transferred file; if major DSP quality improvement breaks parity, attach before/after render and an owner-approved intentional-change record.

## 4. Suggested Architecture Decision Record to record in GitHub

> **ADR: Move to three independent instruments.** Starting with this decision, `VOX Trance Synthesis`, `VOX Drums`, and `VOX FX` are three VST3 instruments with shared build-time libraries and independent GUI/DSP/preset/state. The old Rack 1.0 + `.vins` release direction (#77/#78) is archived as a historical branch and may donate code after audited extraction. It is not the required runtime nor a release blocker for the new products. No automatic merging or deleting of legacy PRs/branches is authorized. Production-ready status requires standalone host tests, not old Rack CI.

**Current operational state:** this document does not itself create tags/branches, close issues, move files, or change release gate statuses in GitHub. Perform step 1–4 through explicit git actions with validation and record their links.


<!-- END 08_LEGACY_RACK_ARCHIVE_AND_REUSE.md -->
