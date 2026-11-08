# FOUND-00 — аудит репозитория и архитектура ULTIMA VOX MUSIC

**Статус:** proposed, только документация. Код, CMake, CI, ID, ABI и ветки не менялись.
**Ветка:** `found/00-audit`. Её база — `docs/ultima-vox-music-suite-2026-11-08` @ `294a128` (#111).
**Дата снятия SHA:** 2026-11-08, `git fetch origin --prune`.
**Входные документы:** `docs/ultima-vox-music/00…08`, `ULTIMA_VOX_MASTER_SPEC.md`; прочитаны полностью.

Все пути под `libs/vox-*`, `products/*`, `cmake/suite/*`, `.github/workflows/suite.yml` — **предлагаемые** (proposed). Таких файлов пока нет.

---

## 1. Состояние remote

### 1.1 Ветки, от которых зависит план

| Ref | SHA | CI `build.yml` (последний run на этом SHA) | Роль в новом проекте |
|---|---|---|---|
| `main` | `d3caaa00aa4ac0b12c16101c0d38e13e55480700` | — | Предлагаемая база `next/standalone-suite` (§3) |
| `docs/ultima-vox-music-suite-2026-11-08` (#111) | `294a128d47ba027a2c9dbceb776fd1f5e3622d4a` | `unstable`; docs only | спецификация |
| `integration/rack-1.0` (#78) | `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1` | success, run 41918656856 | **Основной донор** DSP, UI и тестов. Архив |
| `perf/drums-dsp` (#109) | `be102f87b85c0333efb7e3a6a5a56807bd87995c` | success, run 42351161910 | Оптимизированный Drums DSP. HOLD, P1 открыт |
| `perf/router-channel-index` (#107) | `15456c69b094…` | success, run 41983094116 | Только архив (Rack router) |
| `perf/callback-headroom` (#106) | `c790ded93b98…` | success, run 41983089249 | Методика бенча |
| `integration/acid-303-rack` (#92) | `34f2eb5a041584672c262af98224f8d8d4552359` | **не проверено** (нет в последних 60 runs) | Донор Acid |
| `codex/recovered-acid-20261028` (#69) | `c23b88d2af70c90e38930d7106899144a116d2c5` | не проверено | Донор Acid (UI/controls) |
| `fix/acid-contract-ui-integration` (#59) | `d4fb97663edcf0c5b2340c7c8d2f31b1f9628bfc` | не проверено | Донор Acid |
| `codex/recovered-bass-20261028` (#70) | `a4889aa86dfa2a9d3094d319c9374ee00aab5abd` | не проверено | Донор `FullOnBass.*` (только здесь) |
| `feat/independent-bass-parameters` (#52) | `5de0ad3747a52fc0468a754056e4840eff24adf8` | не проверено | Донор Bass |
| `fx/00-audit` (#110) | `e2f7cef410f7e9953ce80b60451ca171d89e34ee` | success, run 42481542033 | Только справка (V2.5 не реализуется) |
| `fx/base` | `f9c8bc6…` | — | Не используется |

- **Теги:** в репозитории нет ни одного (`git ls-remote --tags` пусто).
- **Ветки `next/*`, `found/*`, `archive/*`:** до этого PR не было.

### 1.2 Открытые PR (30, все Draft) и их зависимости

```
main ← #62 feat/rack-production ← #64 claude/clever-cori-9bsvqy ← #65 ← #66 ← #67 ← #68
      ← #76 ← #79 ← #72 ← #80 ← #81 ← #83 ← #84 ← #85 ← #86 ← #87 ← #88 ← #89
main ← #78 integration/rack-1.0 (staging, DO NOT MERGE)
         ← #92 Acid-in-Rack, #94 Drums design, #108 Drums gap matrix
         ← #106 ← #107 ← #109 (perf), fx/base ← #110
main ← #52 ← #70 (Bass)      main ← #59 ← #69 (Acid)
main ← #111 (suite specification)
```

**Правило архивации:** ни одна base-ветка не удаляется, пока на неё нацелен открытый PR (08 §1.3).

### 1.3 Конфликт: ветка, назначенная сессии
Сессии была назначена ветка `claude/clever-cori-9bsvqy`. Это head PR #64 в стеке Rack. Пуш туда изменил бы Rack-PR, поэтому работа идёт в отдельной ветке `found/00-audit`: так указал владелец («Работай в отдельной ветке»).

---

## 2. Процедура архивации (не выполнена; каждый шаг — с разрешения владельца)

Пример команд — 08 §1. Дополнения FOUND-00:

1. **Аннотированные теги.** SHA сверяются через `git ls-remote` непосредственно перед созданием.

   | Тег (proposed) | SHA | Что сохраняет |
   |---|---|---|
   | `archive/rack-legacy-2026-11-08` | `f9c8bc6` | Rack 1.0 + Drums staging (#78) |
   | `archive/drums-dsp-perf-2026-11-08` | `be102f8` | #109: оптимизация, бенчи, E1/E2 |
   | `archive/acid-303-rack-2026-11-08` | `34f2eb5` | #92 |
   | `archive/bass-recovered-2026-11-08` | `a4889aa` | #70 |
   | `archive/fx-00-2026-11-08` | `e2f7cef` | #110 |

   `main` тегировать не нужно: он остаётся и не переписывается.
2. **Защита тегов `archive/*`** от удаления и перемещения через repository ruleset. Это делает владелец в настройках; у меня нет доступа к настройкам репозитория.
3. **ADR-комментарий** (08 §4) в #77, #78, #110 — только после разрешения.
4. **Ничего не закрывать, не удалять и не ретаргетить.** Закрытие PR как `superseded` — отдельное решение после переноса доноров.
5. **Индекс архива** `docs/ultima-vox-music/ARCHIVE_INDEX.md` (proposed): тег → PR → назначение → CI run → известные дефекты.

---

## 3. Новая ветка и размещение кода

### 3.1 База `next/standalone-suite`

| Вариант | За | Против |
|---|---|---|
| **A. `main` `d3caaa0` + спецификация #111** (рекомендую) | история Rack не тянется; соответствует «no wholesale checkout of Rack code» (06 FOUND-01) | в `main` старый POC-код (`libs/parts`, `libs/generator` и т. п.) и старые версии `libs/acid`, `libs/effects`, `libs/vox-ui`. Доноры берутся **из `f9c8bc6`** в **новые** пути, а не правкой старых файлов |
| B. `f9c8bc6` | все доноры в дереве | наследует весь Rack, `.vins`, ABI, 16 слотов. Противоречит 06 §1 и 08 §2 |
| C. Пустая история (orphan) | чисто | теряется связь с историей и CI; без необходимости |

**Название ветки.** В 08 §1.6 она `next/standalone-suite`, в 01 §K — `architecture/standalone-suite`. Рекомендую `next/standalone-suite`: название из более позднего и более конкретного документа.

### 3.2 Изоляция от legacy
- Старый код **не перемещается** (01 §B1: «не перемещать без проверки CI/путей»). Каталог `legacy-rack/` **не создаётся**.
- Новый код живёт в новых каталогах:

```
libs/vox-dsp/            vox_dsp            C++20, без JUCE: primitives/, effects/, drums/, bass/, acid/, fx/
libs/vox-audio-runtime/  vox_audio_runtime  без JUCE GUI: snapshot handoff, MIDI/transport scheduler, sample store, telemetry
libs/vox-preset/         vox_preset         versioned codec (ядро без JUCE; адаптер ValueTree отдельно)
libs/vox-ui/             vox_ui             существующий каталог; см. §3.4
libs/vox-testkit/        vox_testkit        offline render, golden compare, WAV/feature metrics, alloc probe
products/trance-synthesis/  VoxTranceSynthesis  (juce_add_plugin)
products/drums/             VoxDrums
products/fx/                VoxFX
cmake/suite/             helpers: identity, golden, validator
.github/workflows/suite.yml
```

- **CMake:** корневой `CMakeLists.txt` получает `option(VOX_BUILD_LEGACY "Build the legacy Vox Electronic Engine" ON)` и `option(VOX_BUILD_SUITE "Build the three ULTIMA VOX MUSIC plugins" ON)`.
  - Существующий `build.yml` не меняется и собирает legacy как раньше.
  - Новый `suite.yml` конфигурирует с `-DVOX_BUILD_LEGACY=OFF`.
- **Граница зависимостей.** `vox_dsp` и `vox_audio_runtime` не включают `instrument/`, `rack/`, `vins/`, `native/`, `juce_gui_*`. Проверка в CI скриптом `cmake/suite/CheckBoundaries.cmake` (proposed): grep `#include` по новым каталогам, при запрещённом пути сборка падает.

### 3.3 Цели CMake

| Target | Тип | Зависит от | Содержимое (первый PR, где появляется) |
|---|---|---|---|
| `vox_dsp` | STATIC | std | `Primitives.h`, `EffectsChain` (FOUND-02); drums core (DRM-02); bass/acid (SYN-02/03); FX families (FX-01…) |
| `vox_audio_runtime` | STATIC | `vox_dsp`, `juce_audio_basics` (только `MidiBuffer`) | SnapshotBuffer, event scheduler, sample store, telemetry (FOUND-03) |
| `vox_preset` | STATIC | `juce_core` (только адаптер) | codec `VOXS` (FOUND-03) |
| `vox_ui` | STATIC | `juce_gui_basics`, `juce_gui_extra` | токены, LookAndFeel, компоненты (FOUND-02 → UI-01…05) |
| `vox_testkit` | STATIC (tests) | `vox_dsp` | golden, WAV writer, alloc probe (FOUND-02) |
| `VoxTranceSynthesis` / `VoxDrums` / `VoxFX` | `juce_add_plugin` VST3 | всё выше, статически | FOUND-01 |
| `vox_suite_tests` | exe + CTest label `suite` | testkit, продукты (shared-code targets) | FOUND-01… |

### 3.4 `libs/vox-ui`: проверено на `f9c8bc6`
- **Ядро не зависит от Rack.** `Tokens.h`, `Typography.h`, `VoxPaint.h`, `VoxLookAndFeel.*` (583 строки в `.cpp`), `components/{VoxPanel,VoxKnob,VoxButton,VoxComboBox,VoxTabBar,VoxSectionHeader}` включают только JUCE и друг друга.
- **Единственная Rack-зависимость** — `showcase/VisualGateCapture.cpp` (`#include "PluginEditor.h"`, Rack editor). Не переносится; захват Visual Gate пишется заново против editor продукта (UI-05).
- **Версии расходятся.** В `main` `libs/vox-ui` старее, чем в `f9c8bc6`. FOUND-02 заменяет содержимое версией `f9c8bc6` и фиксирует SHA каждого файла.
- **Нет в коде** (UI-02…04):
  - привязки к параметрам с жестами (`ParameterAttachment`);
  - meter;
  - curve, waveform, step, piano, XY, matrix editors;
  - адаптивная раскладка и DPI.

---

## 4. Матрица повторного использования

Источник — `f9c8bc6`, если не указано иное. Машиночитаемая версия: `found-00/reuse-matrix.csv`.

| # | Донор (путь @ SHA) | Строк | Зависимости (проверено по `#include`) | Тесты-доноры (CTest) | Решение | Открытые дефекты / риски |
|---|---|---|---|---|---|---|
| R1 | `libs/dspcore/Primitives.h` @f9c8bc6 | 142 | `<cmath>`, `<cstdint>` | `graph_demo_parity_tests` (косвенно) | **Reuse as is**. Нужен свой юнит-тест: частота, алиасинг BLEP, устойчивость SVF | Прямых тестов примитивов нет |
| R2 | `libs/effects/EffectsChain.{h,cpp}` @f9c8bc6 | 685 | std only; `std::vector` выделяется только в `prepare()` (`.cpp:77`) | `effects_tests`, `effects_tail_policy_tests` | **Reuse with adapter**: 8 типов, wet/dry, хвосты | Типов, требуемых FX/Synth (EQ, ring mod, trance gate, compressor), нет: это новые модули |
| R3 | `libs/drums/VoxFullOnDrums.c` @f9c8bc6 | 1348 | `instrument/abi/VoxModuleAbiV2.h` (только C-структуры), libc | `fullon_drums_tests`, `tests/fixtures/drums/kit-parity-v1.txt` | **Extract**: DRM-02 сначала компилирует его **статически** через временный C-адаптер (08: «local C adapter possible only during transitional tests»), parity против DRM-00; затем DRM-03 переводит на C++ API `vox_dsp` с тем же golden | 8 пэдов (`PAD_COUNT 8u`), 16 голосов, 187 параметров, state v5. Модуль Rack-специфичен: pool, `.vins` UI-only параметры |
| R3b | то же @be102f8 (#109) | — | — | + `drums_render_equivalence_tests` (108 сценариев, допуск 1e-6 от пика) | **Не донор по умолчанию**: HOLD. Харнесс render-equivalence **переносится** как метод | Оптимизация −24 % (Windows A/B). P1: 96 kHz x8/x16 limit FAIL на эталонном Xeon |
| R4 | `libs/bass/PsyBassVoice.{h,cpp}` @f9c8bc6 | 452 | `juce_audio_basics`, `juce_audio_processors` | `psy_bass_tests`, `bass_render_diagnostics` | **Extract** после SYN-00 golden. JUCE-зависимость убрать: требование «DSP без JUCE» (01 §B2) | Вывод 01/02 про «permanent beep» — проверить на тесте NoteOff |
| R5 | `libs/bass/FullOnBass.*`, `FullOnBassParameters.h` @a4889aa (#70) | — | V2-DLL прототип | `FullOnBass*Tests`, `BassNativeRackTests` | **Только справка до ревью**. На `f9c8bc6` **отсутствует**. 08 §2 указывает его как источник: конфликт, см. §8 К3 | Фиксы MIDI от Codex не ревьюированы |
| R6 | `libs/acid/AcidEngine.*`, `AcidGenerator.*` @34f2eb5 (#92) | 773 + inventory/module | `instrument/InstrumentAbi.h` (V1-структуры) | `acid_tests`, `AcidModuleTests`, `AcidPackageTests` | **Extract, fix reviews**: отвязать от `InstrumentAbi.h` | Последний фикс NoteOff (`34f2eb5`) сделан в **планировщике хоста Rack**, а не в движке. Без нового планировщика (FOUND-03) он не переносится. Нужны тесты short gate / slide / Panic |
| R7 | `libs/lead`, `libs/atmos`, `libs/semanticfx` @f9c8bc6 | 860 / 867 / 799 | `InstrumentAbi.h` | `lead_tests`, `atmos_tests`, `semantic_fx_tests` | **Кандидаты**: Lead → VA (SYN-04), Atmos → Texture/Drone (FX-03), SemanticFx → risers (FX-01/03). Только после golden и прослушивания | Звук не принимался владельцем |
| R8 | `libs/instrument/samples/SampleDecoder.*`, `SampleLoader.*` @f9c8bc6 | 180 / 249 | Decoder: JUCE `juce_audio_formats`, `juce_cryptography`, `instrument/InstrumentContract.h`; Loader: `rack/Rack.h` | `sample_decoder_tests` | **Adapt** в `vox_audio_runtime` (FOUND-03) | Loader привязан к `Rack` (`SampleLoader.h`). Decoder — к `InstrumentContract.h`. Отвязать оба |
| R9 | `libs/instrument/native/SampleExchange.*` @f9c8bc6 | 387 | `VoxModuleAbiV2.h`, `<mutex>` (не-RT сторона) | `sample_exchange_tests` | **Adapt idea**: generation-guarded table + отложенная очистка не в RT | ABI-хэндлы не переносятся |
| R10 | `libs/pattern/Pattern.*` (`SnapshotBuffer`, тройной буфер) @f9c8bc6 | 481 | `instrument/abi/VoxModuleAbiV2.h` (`Pattern.h`) | `pattern_tests` | **Reuse** lock-free handoff (FOUND-03) | Отвязать от ABI-структур |
| R11 | `libs/midi/GeneratedNoteScheduler.*`, `MidiExport.*`, `libs/sequence/*` @f9c8bc6 | 799 / 1041 | JUCE basics, `instrument`, `transport` | `generated_note_scheduler_tests`, `midi_export_tests`, `sequence_tests`, `transport_sync_tests` | **Selective**: тесты-методы и SMF writer | Требование «один источник событий для звука и экспорта» в Rack выполнено **через модуль** (note output). Здесь нужен один планировщик в продукте |
| R12 | `libs/preset/PresetManager.*` @f9c8bc6 | 344 | JUCE, `std::mutex` | `preset_tests` | **Reference** (только не-RT) | mutex недопустим в RT-пути |
| R13 | `libs/modulation/*` @f9c8bc6 | 708 | `instrument` | `modulation_tests` | **Reference** | Rack-macros, 128 pool |
| R14 | `libs/vox-ui/*` @f9c8bc6 | ~935 + components | JUCE GUI | `editor_smoke_tests` (Rack) | **Reuse core** (§3.4) | showcase/VisualGateCapture — Rack |
| R15 | `tests/support/AllocationProbe{Windows,Sanitizer}.c`, `tests/abi/AllocationProbeLinux.c`, `tests/allocation/*` @f9c8bc6 | — | C | `allocation_probe_tests` | **Reuse** в `vox_testkit` | Объект проверки — новый процессор (не Rack) |
| R16 | `tests/performance/CallbackHeadroomBench.cpp` @be102f8 | — | `Rack` API | `callback_headroom_music_smoke` | **Reuse methodology**: классификатор work/preempted/unknown, калибровка, ABAB. Объект — `processBlock` продукта | Старые зелёные результаты не переносятся |
| R17 | `.github/workflows/build.yml`, `perf-ab.yml` | — | self-hosted Windows | — | **Reuse pattern**; новый `suite.yml` | Раннер `1`: 2 сбоя FetchContent JUCE 8 ноября (infrastructure) |
| R18 | `apps/vst3/*`, `libs/rack/*`, `libs/instrument/{vins,native,abi}`, `RackState*`, `docs/fx/*` | ~8000+ | — | — | **Archive / reference only** (08 §2) | — |

**Лицензии исходников-доноров.**
- Все доноры написаны в этом репозитории. Сторонних DSP-исходников в доноре не найдено (проверка по заголовкам и `#include`).
- В репозитории **нет файла LICENSE**. Лицензия собственного кода не объявлена: это решение владельца, связанное с выбором лицензии JUCE (§7).

---

## 5. Идентичность и параметры

### 5.1 Идентичность VST3

**Факт кода JUCE 9.0.1.** `modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h:61`:

```cpp
return VST3Interface::jucePluginId (JucePlugin_ManufacturerCode, JucePlugin_PluginCode, interfaceType);
```

VST3 class ID выводится из пары **manufacturer code + plugin code**. Отсюда:
- `PLUGIN_CODE` трёх продуктов должны различаться между собой и с Rack `DkP1`;
- после первой публичной сборки код **никогда** не меняется: иначе DAW потеряет проекты.

| | VOX Trance Synthesis | VOX Drums | VOX FX |
|---|---|---|---|
| `PRODUCT_NAME` | `VOX Trance Synthesis` | `VOX Drums` | `VOX FX` |
| `BUNDLE_ID` (proposed) | `com.ultimavox.vox-trance-synthesis` | `com.ultimavox.vox-drums` | `com.ultimavox.vox-fx` |
| `PLUGIN_MANUFACTURER_CODE` | `UlVx` (как сейчас) | `UlVx` | `UlVx` |
| `PLUGIN_CODE` (proposed) | `VxTS` | `VxDr` | `VxFX` |
| `COMPANY_NAME` | `Ultima Vox` | `Ultima Vox` | `Ultima Vox` |
| Шины | Main stereo out | Main + 8 stereo aux out (DRM-06) | Main stereo out |
| `IS_SYNTH` / MIDI in | true / true | true / true | true / true |

- Запрещено: `com.ultimavox.vstengine`, `DkP1`, `com.ultimavox.fullon-drums`, `slotNN.pKKK`.
- **Заморозка (FOUND-01):**
  - CTest `suite_identity` читает `moduleinfo.json` каждого собранного бандла (JUCE генерирует его при `VST3_AUTO_MANIFEST`) и сравнивает class ID с `docs/ultima-vox-music/IDENTITY.md`;
  - при несовпадении CI падает;
  - 16-байтовые FUID в `IDENTITY.md` записываются **из бинарника**, а не вычисляются вручную.

### 5.2 Схема ID параметров (единая для трёх продуктов)
- **Формат:** `<prod>.<group>.<name>`, только `[a-z0-9.]`. Префиксы: `syn.`, `drm.`, `fx.`. `juce::ParameterID{id, 1}`.
- **Источник истины:** `products/<p>/parameters.csv` (proposed). Колонки: `id, name, type, min, max, default, skew, unit, choices, automatable, group, since_schema`.
- **Тест:** `suite_parameter_map_<p>` строит `ParameterLayout` и сравнивает с CSV: порядок, ID, диапазон, choices, unit, default.
- **Append-only:** удаление или изменение строки после первого публичного билда — только через migration с тестом.
- **Структурное, не автоматизируемое:** тип движка/семейства, топология, кривые, маршруты, паттерны, сэмплы. Оно лежит в Program state (01 §G; 02 §6; 03 §6; 04 §8).

### 5.3 Карты параметров (уровень групп)

**Расхождение со спецификацией.** 06 §6 требует «actual full host parameter maps» в FOUND-00. При этом 02 §6 фиксирует карту в `Parameters.cpp` при реализации, а 03 §6 — «final map at D-00». Рекомендация:
- FOUND-00 фиксирует **схему и бюджет групп** (ниже);
- полный CSV каждого продукта — **первый коммит** `SYN-01` / `DRM-01` / `FX-01`, до кода процессора, с тестом из §5.2.

#### VOX Trance Synthesis — 164 (цель 02: 130–190)

| Группа | ID-шаблон | Кол-во | Примечание |
|---|---|---|---|
| Global | `syn.global.{gain,trim,poly_glide,bend_range,…}` | 6 | |
| Part A/B: level/pan/transpose/fine/glide | `syn.{a,b}.part.*` | 2×5 | |
| Part A/B: OSC1–3 level/coarse/fine/shape | `syn.{a,b}.osc{1..3}.*` | 2×12 | |
| Part A/B: sub, noise level | `syn.{a,b}.{sub,noise}.level` | 2×2 | |
| Part A/B: Filter 1/2 cutoff/res/drive/env/keytrack | `syn.{a,b}.f{1,2}.*` | 2×10 | |
| Part A/B: AMP и FILTER ADSR | `syn.{a,b}.env.{amp,flt}.{a,d,s,r}` | 2×8 | MOD1/2 ADSR — Program state |
| Part A/B: LFO1–4 rate/depth | `syn.{a,b}.lfo{1..4}.{rate,depth}` | 2×8 | |
| Acid adapter: cutoff/res/envmod/decay/accent/glide/drive/wave | `syn.{a,b}.acid.*` | 2×8 | Отдельные ID: смысл не меняется при смене движка |
| Psy Bass adapter: 13 контролей `FullOnBassParameters.h` @a4889aa (cutoff, resonance, pitch amount/time/curve, ADSR, osc/filter drive, keytracking, output gain) | `syn.{a,b}.bass.*` | 2×13 | Состав подтверждается в SYN-00 |
| FX 4 slots: mix + 2 base | `syn.fx{1..4}.*` | 12 | Тип FX — Program |
| Sends 1/2 | `syn.send{1,2}.level` | 2 | |
| Macros | `syn.macro{1..8}` | 8 | |
| Arp/Seq performance: on/rate/gate/swing | `syn.seq.*` | 4 | |
| **Итого** | | **164** | |

#### VOX Drums — 184 (цель 03: 160–240)

| Группа | ID-шаблон | Кол-во |
|---|---|---|
| Pad 01–16: level, pan, tune, decay, cutoff, reso, send1, send2, mute, transient | `drm.p{01..16}.*` | 16×10 = 160 |
| Returns: delay level/time/feedback, reverb level/size/damp | `drm.ret{1,2}.*` | 6 |
| Master: gain, swing, humanize, generator density/groove/complexity | `drm.master.*`, `drm.gen.*` | 6 |
| Macros | `drm.macro{1..8}` | 8 |
| Transport/perf: pattern A/B, fill, internal play, seq mode | `drm.perf.*` | 4 |
| **Итого** | | **184** |

Модель пэда, сэмплы, note map, choke, выходы и паттерны хранятся в Program state.
**Совместимость с legacy:** у legacy пэда 5 pool-параметров (`PAD_PARAMS 5u`), глобальных 8, sequencer 11. DRM-01 публикует таблицу соответствия legacy → новый ID, но только для parity-теста. Импорта Rack-проектов нет (§5.4).

#### VOX FX — 128 (04 §8)

Стартовая точка — `docs/fx/parameter-map.csv` из #110 @`e2f7cef`: global 24 / layers 4×16 / FX 4×4 / mod 16 / macros 8. Изменения:
- префикс `fx.` по схеме §5.2 вместо pool-индексов;
- семейство слоя и тип FX — Program, не автоматизируемые (то же решение было в #110 §9.2);
- `fx.global.tail` — по правилу хвоста §5.5.

### 5.4 Состояние и миграция (одна политика на продукт)

| | Synthesis | Drums | FX |
|---|---|---|---|
| Контейнер | `VOXS` v1, `product = "syn"` | `VOXS` v1, `product = "drm"` | `VOXS` v1, `product = "fx"` |
| Разделы | params, parts A/B, mod routes, seq/pattern, FX, wavetable refs, seed, ui | params, pads (model, samples by hash + relative path + factory id), note map, choke, outs, patterns, seed, ui | params, program (4 layers, 20 curves, ≤64 routes, 4 FX), samples, trigger pattern, seed, ui |
| Ограничения | размер ≤ 1 MiB (proposed); счётчики ограничены | то же; сэмплы — по ссылке | то же |

Общие правила:
- **Загрузка:** parse → validate (магия, product, schema ≤ current, границы, конечные числа) → prepare (не RT) → commit (атомарная публикация snapshot). При любой ошибке: текущее состояние **не меняется**, editor показывает диагностику.
- **Чужой продукт / более новая схема / мусор** → отказ. Неизвестные **разделы** в той же major-схеме сохраняются как непрозрачные байты (forward-safe).
- **Проекты Rack:** не импортируются. Отказ с явным сообщением (00 §5, 08 §3).
- **Миграция:** только вперёд, одна функция `vN→vN+1` на версию + golden-фикстура каждой версии в `tests/suite/state/`.
- **Пресеты:** тот же кодек. Factory — read-only, user — `%APPDATA%/Ultima VOX Music/<Product>/` (путь из 02 §6, proposed).

### 5.5 Хвост (FX)
`getTailLengthSeconds()` возвращает **фактический** максимум. Он фиксируется после измерения в FX-02 (04 §3: предложено 60 s, «set cap after measured proof»). Ограничения Rack (1 920 000 сэмплов, V2.5 `tailMilliseconds`) к standalone не применяются.

---

## 6. Граф PR

```
#111 spec ─ FOUND-00 (this)
              │
              ▼  (owner: create next/standalone-suite from main d3caaa0 + #111)
           FOUND-01 shell ×3 + identity + suite CI + validator
              ├──────────────┬───────────────────┬────────────────┐
           FOUND-02        DRM-00             SYN-00            UI-00
           dsp/ui/testkit  legacy drums       legacy bass/acid  vox-ui audit
              │            golden corpus      golden corpus       │
           FOUND-03 ───────┼──────────────────┼──────────────── UI-01 → UI-02 → UI-03 → UI-04
           runtime/codec   │                  │                    │
              ├── DRM-01 → DRM-02 → DRM-03 → DRM-04 → DRM-05 → DRM-06 → DRM-07 → DRM-08
              ├── SYN-01 → SYN-02 ∥ SYN-03 → SYN-04 → SYN-05 → SYN-06 → SYN-07 → SYN-08
              └── FX-01 → FX-02 → FX-03 → FX-04 → FX-05 → FX-06 → FX-07
   (*-07 UI-PR каждого продукта требуют UI-04; UI-05 = доказательства в реальных editors)
```

Приоритет релизов (00 §4): **Drums → Synthesis → FX**. Каждый PR — Draft, base = предыдущий PR цепочки или `next/standalone-suite`. Merge — только по явному разрешению владельца.

### 6.1 Первые три Draft PR

**PR-1 · FOUND-01 — три пустых, но загружаемых VST3**
- **Файлы:**
  - `CMakeLists.txt`: опции `VOX_BUILD_LEGACY` / `VOX_BUILD_SUITE`, `add_subdirectory(products/…)`;
  - `products/{trance-synthesis,drums,fx}/{CMakeLists.txt,Processor.*,Editor.*}`;
  - `cmake/suite/*`;
  - `docs/ultima-vox-music/IDENTITY.md`;
  - `.github/workflows/suite.yml`;
  - `tests/suite/*`.
- **Поведение:**
  - процессор отдаёт тишину;
  - editor 1180×760 с `VoxLookAndFeel`, без контролов; заглушек «Generate» нет;
  - state `VOXS` v1 пустой: round-trip, отказ на чужой и битый state.
- **Тесты (CTest label `suite`):**
  - `suite_identity`;
  - `suite_lifecycle` (prepare / process / release на 44.1/48/96 × 32/64/128/512 и блок 0);
  - `suite_state`;
  - `suite_boundaries`.
- **Windows (self-hosted, `suite.yml`):**
  1. configure MSVC Release `-DVOX_BUILD_LEGACY=OFF`;
  2. build 3 VST3;
  3. `ctest -L suite`;
  4. **Steinberg validator:** собрать target `validator` из `steinbergmedia/vst3sdk` на закреплённом теге (MIT) и прогнать на трёх бандлах. Validator в урезанном VST3_SDK внутри JUCE отсутствует (проверено). Тег SDK — **не знаю**, выберу и закреплю в PR;
  5. SHA-256 бандлов в артефакте.
- **Изменений `build.yml`, legacy, ID Rack — нет.**

**PR-2 · FOUND-02 — перенос DSP и UI без изменения звука**
- **Перенос:**
  - `git show f9c8bc6:<path>` → `libs/vox-dsp/primitives/Primitives.h`, `libs/vox-dsp/effects/EffectsChain.*`;
  - `libs/vox-ui/*` ← версия `f9c8bc6`.
  - SHA каждого файла записывается в `libs/*/PROVENANCE.md`.
- **`vox_testkit`:**
  - оффлайн-рендер, WAV writer;
  - сравнение: допуск 1e-6 от пика плюс feature-метрики (RMS, пик, спектральный центроид);
  - alloc probe.
- **Golden:**
  - `EffectsChain` — каждый тип × 3 частоты дискретизации: рендер нового target сравнивается с рендером, собранным из `f9c8bc6` в том же job (A/B в одном прогоне);
  - примитивы — частота, алиасинг, устойчивость.
- Тесты-доноры `effects_tests` и `effects_tail_policy_tests` переносятся без ослабления.

**PR-3 · DRM-00 — эталонный корпус legacy Drums** (параллельно PR-2)
- **Задача:** зафиксировать звук 8 пэдов `VoxFullOnDrums.c` @`f9c8bc6` до любого переноса.
- **Как:** job собирает legacy-модуль на `f9c8bc6` (отдельный checkout) и тестовый рендерер из suite-ветки. Рендерер грузит legacy-модуль **только в тесте**.
- **Корпус:**
  - каждый пэд × velocity {1, 64, 127};
  - каждая фабричная кит-настройка и паттерн 4 такта;
  - 44.1/48/96 × 64/128/512.
- **Результат:**
  - в git — manifest: хэш, метрики, параметры;
  - WAV — артефактом CI (не в git).
  - Плюс таблица «legacy-параметр → смысл».
- **Почему так:** DRM-02 сравнивает статически перенесённый код с этим manifest.

---

## 7. JUCE: лицензия и зависимости

- **JUCE 9.0.1** — FetchContent, `GIT_TAG 9.0.1`, commit `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`. `LICENSE.md` в исходнике: **AGPLv3 или коммерческая JUCE 9 EULA**.
  - Закрытая продажа плагинов под AGPLv3 невозможна без раскрытия исходников. Нужна коммерческая лицензия JUCE (01 §L: «release blocker»).
  - Условия тарифов JUCE 9 я **не проверял**. Это юридическое решение владельца.
  - Для разработки и внутренних сборок решение **не блокирует** работу.
- **Внутри JUCE:** VST3 SDK — MIT. ASIO и AAX не используются: VST3-only, без `juce_audio_devices` в продуктах.
- **Закрепление:** `GIT_TAG 9.0.1` — мутабельная ссылка на тег. FOUND-01 переходит на commit SHA `e18f7f5…` (01 §I: pin by commit).
- **Инфраструктура:** 8 ноября на раннере `1` дважды оборвался `git clone` JUCE. Предложение для FOUND-01: `FETCHCONTENT_SOURCE_DIR_JUCE` указывает на заранее выкачанную копию на раннере, а job сверяет SHA. Это изменение CI — только с разрешения.
- **Новые зависимости:**
  - `vst3sdk` (MIT) — только для validator в CI, не линкуется в продукт;
  - pluginval — **не знаю** его текущую лицензию; проверю до подключения.
  - Иные библиотеки — только через отдельное ревью лицензии.

---

## 8. Конфликты спецификации с кодом

| # | Где | Конфликт (факт) | Варианты | Рекомендация |
|---|---|---|---|---|
| К1 | 01 §K vs 08 §1.6 | имя ветки `architecture/standalone-suite` vs `next/standalone-suite` | одно из двух | `next/standalone-suite` |
| К2 | 06 §6 vs 02 §6, 03 §6 | полные карты параметров в FOUND-00 vs при реализации / в D-00 | (a) полные CSV сейчас; (b) схема + бюджет сейчас, CSV первым коммитом *-01 | **(b)**, §5.3 |
| К3 | 08 §2 (Bass) | `libs/bass/FullOnBass.*` указан как источник, но на `f9c8bc6` и `main` его **нет**. Есть только на `a4889aa` (#70, Codex, без ревью) | (a) `PsyBassVoice` @f9c8bc6 как донор, FullOnBass — справка; (b) ревью #70 в SYN-00 и перенос | **(a)**, затем (b) в SYN-00 |
| К4 | 03 §1, 08 §2 (Drums) | «PR #72 + staged #95–#104» уже влиты в `f9c8bc6`. Оптимизированная версия — #109 `be102f8` (HOLD) | донор `f9c8bc6` или `be102f8` | **`f9c8bc6`**, 06 FOUND-02: «no unreviewed DSP optimization». Харнесс #109 — как метод |
| К5 | 08 §2 (Native C ABI: Archive) vs перенос `VoxFullOnDrums.c` | модуль использует структуры `VoxModuleAbiV2.h` | (a) временный статический C-адаптер в DRM-02, затем C++ API в DRM-03; (b) сразу переписать на C++ | **(a)**: parity доказуем, ABI не публикуется, DLL нет |
| К6 | 01 §B1 | `legacy-rack/` в дереве vs «не перемещать старые исходники» | перемещать или нет | **не перемещать**, §3.2 |
| К7 | 01 §B2 «DSP без JUCE» | `PsyBassVoice.h` включает `juce_audio_basics/processors` | отвязать или оставить | отвязать в SYN-02 под golden |
| К8 | 04 §3 tail 60 s vs Rack 1 920 000 сэмплов | ограничение Rack к standalone неприменимо | — | §5.5: фактический cap после измерения в FX-02 |

---

## 9. Тестовые фикстуры и что пока нельзя проверить

**Есть (`f9c8bc6`, команды):**

```
cmake -S . -B build -A x64 && cmake --build build --config Release --parallel
ctest --test-dir build -C Release -R "^(fullon_drums_tests|effects_tests|effects_tail_policy_tests|psy_bass_tests|acid_tests|lead_tests|atmos_tests|semantic_fx_tests|pattern_tests|sample_decoder_tests|sample_exchange_tests|generated_note_scheduler_tests|midi_export_tests|allocation_probe_tests|bass_render_diagnostics)$" --output-on-failure
# be102f8: + drums_render_equivalence_tests, callback_headroom_music_smoke
```

- **Фикстуры:**
  - `tests/fixtures/drums/kit-parity-v1.txt`;
  - `tests/fixtures/samples/click.wav` — единственный WAV в репозитории;
  - render equivalence 108 сценариев (#109);
  - Acid тесты на #92 (`AcidModuleTests`, `AcidPackageTests`);
  - Bass на #70.
- **Бенч:** E1 (Xeon E5-2680 v4) — только справка, `docs/testing/callback-headroom-reference-xeon.md` на #109.

**Нельзя проверить сейчас:**
- качество звука, жанровое различие, «Acid/Bass sound acceptance» — нужен слушатель-владелец;
- Cubase/REAPER: загрузка, автоматизация, multi-out, bounce — нужна DAW-машина владельца;
- CPU на целевой DAW-машине (S2 из #109 не получен);
- лицензии фабричных сэмплов: их в репозитории нет, источник не выбран (00 §7);
- что сборки на #92/#70 зелёные: последние runs на их SHA не найдены.

---

## 10. Решения владельца, блокирующие работу

| # | Решение | Что блокирует | Рекомендация |
|---|---|---|---|
| D1 | Создать `next/standalone-suite` от `main` `d3caaa0` + #111 (вариант A, §3.1) | FOUND-01 | да |
| D2 | Идентичность §5.1: `BUNDLE_ID`, `PLUGIN_CODE` `VxTS` / `VxDr` / `VxFX`, `UlVx` | FOUND-01 (заморозка) | утвердить или дать свои коды |
| D3 | Донор Drums = `f9c8bc6` (не `be102f8`) | DRM-00 | `f9c8bc6` |
| D4 | Bass: `PsyBassVoice` @f9c8bc6 сейчас, #70 — ревью в SYN-00 | SYN-00 | да |

**Не блокируют разработку**, но нужны до релиза или архивации:
- лицензия JUCE (коммерческая или AGPLv3);
- создание тегов §2 и ruleset;
- ADR-комментарии в #77/#78/#110;
- источник лицензированных сэмплов.

---

*No ABI/host/ID changes in this PR. No production code, CMake or CI touched.*
