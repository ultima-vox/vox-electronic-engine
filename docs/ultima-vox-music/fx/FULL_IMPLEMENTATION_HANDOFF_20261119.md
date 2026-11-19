# VOX FX — сводная инженерная передача (19 ноября 2026)

**Статус: инженерный кандидат, не релиз.** Ветка передачи `arena/20b960e7-vox-electronic-engine`. Последний коммит с изменениями кода — `cde87af7f8413824bf849c76bd792beecd7f8635`; коммит, содержащий этот файл, добавляет только документацию и результаты. PR [#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156) оставлен Draft/HOLD на `f042f955aee724636037471647a7c3be664e18bc`, без изменения его ветки, тела или opt-in. Цели Wave A/B/C не сокращены.

## Что изменено

- **Нативный контур:** реальный `FxProcessor` и `FxEditor` собраны и исполнены с pinned JUCE при стеке 1 MiB. Проверены изменение графа Motion, выбор/генерация после Factory, поиск и User save/load, stale gesture при host restore, undo, reopen без редактора и offscreen layout. Это не VST3-host и не Windows DPI.
- **DSP:** 64 типизированных маршрута, два LFO с Hz/tempo, S&H, event step/ADSR/phase/velocity, dry follower, восемь прежних host macros. Отдельная область события и микса; событие удерживает граф при запуске, mix/FX оценивают текущий. Общий post-DSP у live и export. 12 семейств получили слышимый signed Character; legacy `0` нейтрален. FPRG v6 читает v1–v5, FMOD v2 читает v1. Питч/тональность и два фильтра на слой сохранены. Все 19 прежних host ID и CI workflow без изменений.
- **Пользовательский контур:** целевой Role-aware Generate/Mutate с полями и graph locks, persistent A/B; native Review/User browser, текстовый поиск/фильтры, теги, избранное, сохранение/удаление. `.voxfx` v2 добавляет опциональный META, старый v1 читается. Preview рендерится из отдельного снимка с SHA-проверкой WAV, ограничен по длине и не загружает Program/параметры DAW; он **замещает realtime-выход плагина**, поэтому должен быть остановлен до записи. Хостовый non-realtime bounce его не включает. Pattern/SMF и bounded WAV capture/atomic export продолжают работать; OS drag не заявлен.
- **Контент:** ещё 16 отдельных авторских DSP-рецептов; всего 32 review-only. Отдельно от 48 seed-программ. Нет заимствованных WAV в этих рецептах и нет человеческого одобрения на распространение. В [манифесте](evidence/review-catalog-52a60d3.tsv) указаны ID/revision, role/style/mood/mode и происхождение; [SHA-256](evidence/review-catalog-52a60d3.sha256) и [метрики](evidence/review-catalog-52a60d3-metrics.txt) привязаны к снимку кода. Это не новый утверждённый factory release.

## Матрица FX-01…07 / FX-T01…20

Обозначения снимков: **S** = `52a60d3851ffee4ef09cb0223869e7fbfb65db96` (standalone, каталог). **N** = `cde87af7f8413824bf849c76bd792beecd7f8635` (native, workers, sanitizer). Между S и N исправлена только сигнатура cancellable WAV loader в `SampleDecode.h` и его использование в `Preview.h`; standalone-аудит их не включает. **Ист.** = `962d5efa4bcde84c4536ad0d5f0e6111c4002581`, [прошлая квалификация](SOURCE_WORKFLOW_PASS_20261119.md).

| ID | Реализовано / исполнено | Аудио и остаток |
|---|---|---|
| FX-01 | Нативный processor/state, старые automation ID; N `FxProcessorTests` | Noise Riser звучит; VST3 wrapper/identity на Windows не исполнены здесь |
| FX-02 | Четыре слоя, 20 curves, шесть режимов; S `Layer`, `CurveTrajectory`, `PlaybackMode`, `Transport`; N processor | Есть 32 clip-рендера; host/UI choreography ещё не вся квалифицирована |
| FX-03 | 12 семейств, Character, key/pitch, dual filters; S/N `FxSourceSoundTests` | Различия волн измерены; музыкальный слуховой и spectral audit каждого семейства не закрыт |
| FX-04 | Motion native, share live/export, 4 bounded effects; S `MacroRouting`, `Effects`, `SoundGraph`, N stress | Нет полной квалификации anti-alias, sync и parity для всех FX; step/ADSR shape фиксированы |
| FX-05 | Generate/Mutate, A/B, browser/preview, WAV/SMF; N processor, worker/export, S state/pattern | Частично: 32 вместо 120/300/600, неполный набор facets/метаданных и OS drag не квалифицирован |
| FX-06 | Native controls, telemetry, resize/offscreen; N processor | Реальный keyboard/focus/scroll и Windows DPI не заменены offscreen-тестом |
| FX-07 | Linux engine/native/sanitizers, аудио и хэши | Не закрыт: Windows, валидатор, human listening, DAW и installer |
| FX-T01 | S/N `FxSourceSoundTests` + family tests | 12 аудиореакций; нет утверждённых слуховых golden refs |
| FX-T02 | S `FxLayerTests`, `FxProgramStoreTests`, `FxSoundGraphTests` | 4-layer/snapshots; native automation in host остаётся |
| FX-T03 | S codec/20 trajectories; N processor: stale drag/restore, undo/redo | Не все действия для каждой curve в реальном host |
| FX-T04 | S `FxPlaybackModeTests`, `FxPatternPlaybackTests`; N processor | Представлены все 6 modes; DAW clock/seam listening отсутствуют |
| FX-T05 | S playback/modes; N stress: FIFO/steal cases, CC120/123, velocity-zero и panic | Не эквивалентно click/listening-аудиту каждого предела |
| FX-T06 | S `FxTimelineTests`, `FxBarEventTests`, `FxTransportTests`; N synthetic playhead | Матрица баров/темпов, seek/clock policy, не тест Cubase/REAPER |
| FX-T07 | S/N source & processor: pitched ratio, roots/filters и non-tonal tagging | Не полный harmonic/listening audit |
| FX-T08 | S/N macro/graph, N native matrix, 64 маршрута | Scope и автоматизация проверены для выбранных маршрутов; нет полного Cartesian аудита всех маршрутов и редактируемых форм step/ADSR |
| FX-T09 | S effects/bench/offline; N callback с четырьмя Drive | Ист. peak каскада `1.000→0.278`, full-scale 38 558/38 297/38 200→0/0/0; нет полного alias/fold-down/host tail audit |
| FX-T10 | S sample store/lease, N WAV worker/library и TSan, до 64 grains | Missing/hash/relink/lease покрыты тестами; DAW restore и все UI worker races ещё не квалифицированы |
| FX-T11 | S generator/state, N processor и preset library: tags/query, A/B, structural vs host bases | Нет полного перебора locks по каждой комбинации, всех facets и длительного UI I/O stress |
| FX-T12 | S 32 distinct PCM, 96 render checks + 32 повтора | 0 approved; 120/300/600 не достигнуты; не проверены на слух транзиенты, tails и музыкальная пригодность |
| FX-T13 | S grid/swing/pattern/scheduler, N processor structural clipboard/export | Editable SMF в файле, не перенос drag/drop в DAW |
| FX-T14 | S offline, N linked file/WAV export: rate/format, capture и cancel | Нет полной нативной same-take live↔offline parity по режимам; OS drag не заявлен |
| FX-T15 | N живой редактор offscreen и resize/scale smoke | Не полная accessibility, keyboard/scroll, не DPI ОС |
| FX-T16 | N project round-trip/closed editor, invalid/future rollback, sample workers | Не project reopen в DAW; остаток — отсутствующие ресурсы в пользовательских путях хостов |
| FX-T17 | Не исполнен на N | Windows Release CTest, Steinberg validator, VST3 hash и exception traces отсутствуют |
| FX-T18 | N 27 stress cells с probes; S три benchmark; N ASan/UBSan/TSan | Функциональные пороги пройдены; deadline **не квалифицирован**, см. ниже |
| FX-T19 | S manifest + asset-free procedural recipe source; recipe/revision/WAV hash | Нет утверждения лицензирования и provenance для полного stock 600 |
| FX-T20 | Не проводился | Единственная финальная DAW-приёмка остаётся у владельца |

## Исполненные команды и результаты

[Сводный лог запусков и ошибок](evidence/motion-browser-qualification-20261119.txt): SHA-256 `2e705749a2d4d1545d5e047c277f43653f293b84b0f14717eb89c4055c45aee1`.

- **S, GCC 12.2:** `g++ -std=c++20 -O2 -pthread -DVOX_TEST_INTERPOSE_MALLOC=1 -Iproducts/fx -Ilibs/vox-preset -Ilibs/vox-testkit/probe tests/suite/$name.cpp libs/vox-preset/StateContainer.cpp probe.o -ldl -o "$name"` (`probe.o` — GCC `-O2` из `AllocationProbeLinux.c`). `(ulimit -s 1024; ./"$name")`. **39 тестовых исполняемых файлов + 3 benchmark**, exit 0. Для каталога: `FxFactoryCandidatesTests OUT/catalog`; `(cd OUT/catalog; sha256sum *.wav manifest.tsv)`. Native и JUCE-dependent workers вынесены в N.
- **N:** G++ C++20, `-O2` для FX, `-O1` для модулей, JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`, Linux X11 runtime; recipe [run-linux-native-fx.sh](evidence/run-linux-native-fx.sh) воспроизводит scoped link; здесь использовались уже собранные объекты той же JUCE. `(ulimit -s 1024; ./FxProcessorTests)` — **0 failures**, `(ulimit -s 1024; ./FxRuntimeStressTests)` — **0 функциональных failures** в 27 ячейках; четыре связанные worker-программы (`FxFileExportTests`, `FxSampleDecodeTests`, `FxSampleControllerTests`, `FxPresetLibraryTests`) — **4/4**, `-O1` с pinned JUCE core/crypto. Это не full Linux CTest.
- **N sanitizers:** повторная компиляция целевых тестов с `-O1 -g1 -no-pie -pthread -fno-omit-frame-pointer -fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1` — **12/12**; `-fsanitize=thread`, `TSAN_OPTIONS=halt_on_error=1` для `FxProgramStoreTests FxSampleStoreTests` — **2/2**. Для preset library внешние JUCE core/crypto объекты не инструментированы.
- **Timing:** Xeon 2.60 GHz, Linux, 2 видимых CPU. Функционально: 64/128/512, 44.1/48/96 kHz, 1/4/8 стартовых голосов, random MIDI, 4 granular layers/до 64 grains/4 Drive/64 motion routes. Ноль измеренных аллокаций и lock-вызовов в Linux callback probes (pthread mutex/trylock и rwlock в связанных объектах, не глобальное наблюдение ОС). Peak ≤0.105386, DC ≤0.0010422. Изолированный повтор при 48 kHz × 128 × 8: p50/p95/p99/max **417.82/561.38/589.84/598.19 µs**; при 96 kHz × 64 × 8: **238.79/294.67/310.37/321.87 µs**. Сравнение с физическим бюджетом `frames/rate`: в изолированном повторе **1/27** ячеек превысила его по max (44.1 kHz × 64 × 4: **2405.53 µs** при **1451.25 µs**); в первом запуске параллельно со сборкой — **25/27**. Timing не являлся условием exit code stress-теста; это **не** RT deadline PASS. Max-ratchet покрыт отдельным scheduler benchmark, не совмещён с этим native stress.
- **Отрицательные прогоны:** у раннего native запуска — два FAIL (editor generate/family selector и FXSL v1 expectation), устранены и перепроверены в N. Нативная S сборка вернула ошибку сигнатуры `sample::loadWav`; N восстановил прежний двухаргументный контракт. Полный строгий Linux configure всё ещё **exit 1**, `Could NOT find PkgConfig` в JUCE: `cmake -S . -B build/fx-native-suite -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON -DFETCHCONTENT_SOURCE_DIR_JUCE="$PWD/build/juce-fx"` (CMake 3.31). Исторические Windows artifacts `44172558893` и `44376476136` содержат VST3, не crash dumps. Переход на heap и Linux 1 MiB — не доказательство причины Windows SEGFAULT.

## Библиотека и аудио

| Роль | Review-кандидаты | Утверждено | Цель 600 |
|---|---:|---:|---:|
| Impact | 4 | 0 | 110 |
| Riser | 4 | 0 | 95 |
| Fall | 4 | 0 | 65 |
| Sweep | 4 | 0 | 70 |
| Zap | 4 | 0 | 105 |
| Rhythm | 4 | 0 | 75 |
| Atmosphere | 4 | 0 | 55 |
| Stinger | 4 | 0 | 25 |
| **Итого** | **32** | **0** | **600** |

Режимы: one-shot 14, Gate 5, Continuous 4, Loop 2, Sequence 4, Transition 3. Рендеры проверены на 44.1/48/96 kHz по peak/DC/непустому PCM и distinct 48-kHz fingerprints. Все 16 прежних WAV при 48 kHz **побайтно совпали** с `962d5ef`. Не следует принимать проверку PCM за оценку качества звука или доказательство отсутствия NaN до кодера во всех рецептах. Архив для прослушивания: `build/fx-52a60d3/review-32-wavs.zip`, 17 604 059 байт, SHA-256 `d170437d7081fb403ef9c959bbcc9075aaa9216d4c135e1f4694dfcc6b8ed2d1`; не включён в Git, восстанавливается из `FxFactoryCandidatesTests` и каталога хэшей. Для Wave A ещё **88** кандидатов даже до прослушивания/отбора; ни одна волна не зачтена.

## Внешние блокеры и следующий крупный пакет

1. **P0 — Windows exact-head и historical exception evidence.** Runner/workflow `.github/workflows/suite.yml`; PR [#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156) сейчас на другом SHA. После TL reconciliation и готовности интеграции — один `[RUN_FX_SUITE]` запуск. Если повторятся четыре падения, сохранить exception/stack/minidump для noise riser, processor, layer и Shepard; затем CTest, Steinberg и SHA-256 VST3. Старый PR head не выдавать за проверку нового.
2. **P1 — полный Linux CTest / реальные display peers.** Строгая команда выше останавливается в `JUCEModuleSupport.cmake` на `PKG_CONFIG_EXECUTABLE`; scoped harness не воспроизводит нативные dialogs, DPI и focus OS. Повторить с полноценным dev sysroot и X display; Windows — 100/125/150/200% на целевых размерах.
3. **P1 — прослушивание и решение о распространении.** Воспроизвести архив по manifest/хэшам; ответственный слушатель фиксирует решение на `ID + revision + WAV hash`. Это не блокирует создание следующих авторских рецептов, но блокирует approved counts.
4. **P1 — финальная owner-only DAW stage.** После интегрированного кандидата и независимого TL review: один цикл Cubase 12–15/REAPER с проектным restore, automation, loop/seek/stop, MIDI/WAV transfer и install/uninstall, включая Drums+Synth.

**Следующий крупный незавершённый пакет:** довести Wave A до 120 отдельных авторских кандидатов, не seed-вариантов, параллельно закрывая native browser facets и проверку live↔offline/tail parity. До выхода на финальный Windows gate дополнить тест burst+max-ratchet, измерить дедлайны изолированно и сопоставить с предобъявленными Windows-бюджетами. Загруженный здесь Linux результат, слуховую оценку и DAW sign-off не смешивать.
