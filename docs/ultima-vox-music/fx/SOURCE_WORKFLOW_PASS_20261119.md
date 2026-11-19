# VOX FX — источники, сравнение, пользовательские пресеты и Pattern

**PR #156: Draft / HOLD, не RC.** Ветка публикации `arena/20b960e7-vox-electronic-engine`. SHA кода и тестов: `962d5efa4bcde84c4536ad0d5f0e6111c4002581`. Следующий коммит содержит только этот отчёт и доказательства. Сравнение с предыдущим [handoff](IMPLEMENTATION_PASS_20261119.md) и [ТЗ](VOX_FX_UNIFIED_PRODUCTION_TZ.md) на `a23b67b02264ca42f2b9bf5f9fed8e233a31fe35`: ТЗ не менялось. 19 ID параметров и `[RUN_FX_SUITE]` без изменений. VOX Drums и VOX Synth не затронуты.

## Что доведено до работающего кода

- **Source / Key.** `SourceSound.h`, `NoiseRiser.h`, `ProgramCodec.h`, `ProgramStore.h`, `FxEditor.*`: хроматика / мажор / натуральный минор / дорийский / фригийский лад, root, транспозиция и два последовательно включаемых SVF на слой. В режимах LP/HP используются наклоны 12/24 dB/oct; для BP/notch это одна/две секции, а не обещание тех же склонов. Частота ограничивается `0.45 × Fs`, Q 0.5–4. Изменения используются только при новых событиях; у каждого голоса собственное состояние фильтров. Гранулы суммируются в своём слое до фильтрации. FPRG v5 мигрирует v1–v4 с хроматикой, нулевой транспозицией и выключенными фильтрами. Для modal-банка транспозиция округляется до полутона. Продолжают действовать аппаратные/математические ограничения существующих семейств: например, крайние ноты ограничиваются рабочим диапазоном осциллятора, а шумовой Riser не становится тональным от выбора root.
- **A/B и пресеты.** `ComparisonState.h`, `PortablePreset.h`, `Processor.*`: два слота FXAB независимы, сохраняются в проекте, ограничены по размеру и не могут включать вложенный FXAB. При A/B recall восстанавливаются базовые значения хост-параметров. Отдельный `.voxfx` (VXUP v1) сохраняет структурный Program, Pattern, маршруты, типы FX, настройки генератора и ссылку на сэмпл, но не меняет 19 хостовых значений, размеры редактора или A/B. Файл проверяется (схема, размеры, CRC, разделы), запись через временный файл. Сэмпл не встраивается. Native меню умеет сохранять/загружать пользовательские файлы и загружать review-рецепты. Это **не** изолированный preview: загрузка меняет структурное состояние текущего проекта. Search/tag/delete и полноценный браузер ещё отсутствуют.
- **Pattern.** `Pattern.h`, `ProgramStore.h`, `FxEditor.*`: копирование/вставка всей сетки с меткой формата, история до 32 изменений, drag сворачивается в одну запись. Защищённая замена проверяет и показанный Pattern, и ревизию Program; undo/redo сбрасываются при внешней замене. Правки по-прежнему экспортируются через общий SMF-планировщик, а не отдельное округление UI. Структурные слайдеры Source/Pattern отправляют значения синхронно, чтобы отложенная нотификация не превратилась в запись после restore.
- **Каталог.** 8 новых отдельных ручных рецептов, 16 всего; ни одного автоматически утверждённого. Добавлены детерминированный TSV-манифест, проверки точных аудиодубликатов и честный признак pitched-source (в старом Salt Air есть синусоидальный компонент). Audit не заменяет перцептивную проверку дубликатов, loudness/aliasing или прослушивание.

## Матрица FX-01…07

В колонке «Исполнено» SHA по умолчанию `962d5ef` (Linux, см. лог); `syntax` не означает исполнения JUCE редактора. Обозначение «кандидат» не означает approval.

| Область | Реализация | Исполнено | Аудио-кандидат | Не закрыто |
|---|---|---|---|---|
| FX-01 | 12 семейств / native панели | семейства, syntax | отдельные семейства и гибриды | исполняемый редактор и DAW |
| FX-02 | 6 режимов, MIDI FIFO, часы, key/scale | transport/modes/bar/source | все 6 режимов в review | host transport и края диапазона в DAW |
| FX-03 | 20 кривых, типизированные фильтры и pitch, SHA-релинк | codec/snapshot/curve/spectral, linked sample workers | FX-гибриды и фильтрованные гранулы | расширенные source-specific controls, filter drive, UI/DAW parity |
| FX-04 | shared FX, Drive compensation, частичная modulation | FX/macro/RT probes | Drive и другие эффекты в WAV | полная native 64-route matrix, alias/deadline/no-lock |
| FX-05 | генератор, FXAB, `.voxfx`, Pattern, WAV/SMF, кандидаты | codec/generator/SMF/WAV/audit | 16 WAV 48 kHz | независимый preview/search/tag/delete, полные генерация/locks, 120→300→600 |
| FX-06 | guarded structural UI, project recall | standalone guards и native syntax | — | закрытый редактор/drag/clipboard/DPI исполняемым JUCE-тестом |
| FX-07 | gate не менялся | Windows/validator/DAW не исполнены | 0 approved | Windows-кандидат, TL и финальная приёмка владельца |

## Матрица FX-T01…20

| Тест | Реализовано | Исполнено / доказательство | Аудио-кандидат | Не закрыто |
|---|---|---|---|---|
| T01 | 12 разных семейств | семейные DSP тесты и callback bench | семейные/смешанные | человеческая оценка характера и anti-alias golden |
| T02 | 4 слоя / параметры / неизменяемые голоса | Layer/SourceSound/ProgramStore; совпадение active-note samples до/после правки | hybrid | расширенные source-specific controls |
| T03 | 20 кривых, системные editor bindings, FPRG v5 | Codec/Trajectory/Store, syntax native | есть curves в WAV | JUCE drag/undo, automation readback |
| T04 | One Shot, Gate, Continuous, Loop, Sequence, Transition | PlaybackMode/BarEvent/PatternPlayback | все 6 offline | DAW и слуховая оценка швов |
| T05 | FIFO/steal/fade/panic | standalone голоса; native MIDI fixtures только syntax | — | CC120/123 и velocity-zero через реальный wrapper |
| T06 | 4/4 и политика clock loss | Transport/BarEvent/PatternPlayback, матрица баров и BPM | tempo-fixed | реальные host seek/loop/3/4 |
| T07 | root/scale/transpose | SourceSound: 6144 scale points и waveform pitch parity | tonal / noise-only Ash Tide | полный UI readback и семейные pitch golden |
| T08 | 8 host macro bases, ограниченный набор маршрутов | Modulation/MacroRouting standalone | — | native full sources/destinations/matrix и host automation |
| T09 | четыре FX, хвосты и Drive компенсация | EffectsTests, Effects/Pattern bench; [старый baseline](IMPLEMENTATION_PASS_20261119.md#four-serial-drive-slots-reproduced-then-corrected) | Drive, reverb, delay и др. | sync, alias/listening, реальная автоматизация |
| T10 | 64 grains, текущая генерация sample leases, SHA relink | Granular и Sample* плюс linked workers | built-in granular | реальный host reopen/missing prompt |
| T11 | A/B, `.voxfx`, Generate/Mutate/locks | PortablePreset/Generator/GeneratorState; JUCE restore fixtures syntax | review menu | полный lock coverage, preview/search/tag/delete |
| T12 | ID/роль/style/mode/provenance, аудитация 16 | FactoryCandidates 3 Fs/повтор WAV/duplicate | 16 review WAV | 120/300/600, perceptual duplicate и слушатель |
| T13 | 1–4 бара, swing/ratchet и clipboard/undo | Pattern* + Store/SMF, native syntax | два rhythm | реальный clipboard/drag/undo |
| T14 | WAV/SMF, отмена и атомарная публикация WAV | OfflineRender/linked FileExport, Pattern* | PCM24 48 kHz | host file-transfer и OS drag |
| T15 | native панели и структурные события | только pinned JUCE syntax | — | native execution, focus/scroll/resize/100–200% DPI |
| T16 | FPRG v1–v5 и ограниченный FXAB | Codec/Store/PortablePreset; проектные JUCE тесты написаны, не исполнены | — | закрытый редактор и полная rollback интеграция |
| T17 | opt-in Windows gate сохранён | не исполнено | — | Windows x64 CTest/VST3/validator и происхождение артефактов |
| T18 | bounded nonallocation granular/voice | CallbackBenchmark с probe, 54 комбинации bypass/dual-filter | — | no-lock и consolidated random MIDI, Windows deadline |
| T19 | оригинальные рецепты, без внешних файлов | TSV-манифест и SHA репрезентативных WAV | 16, не approved | формальная проверка прав и дистрибуция |
| T20 | отложено до TL review | не начато | — | финальная разовая Cubase 12–15 / REAPER приёмка владельцем |

## Исполненные команды, измерения и артефакты

На `962d5ef`: **41** самостоятельный Linux-исполняемый файл (38 тестов + 3 диагностических benchmark; стек 1 МиБ), **3** связанных с pinned JUCE worker-теста, **10** ASan/UBSan и **2** TSan (ProgramStore, SampleStore) прошли. Native processor/editor/test translation units прошли `-fsyntax-only`. Полный CTest не сконфигурирован. Успешные и неуспешные команды, stdout, характеристики среды и результаты в [логе](evidence/source-workflow-20261119.txt), SHA-256 `1d01946dd30362fbf358300602c25bc7122b0a8946ce154a61aba31a9d2b2b1a`.

- **Проверка миграции звука:** все 8 предыдущих 48 kHz PCM24 audition WAV совпали побайтно с `cdd138f`. Это регрессия на выбранных рецептах, не доказательство идентичности любых старых проектов. Новые фильтры измерены синусом на 44.1/48/96 kHz: LP 1 kHz на 6 kHz имеет RMS < 0.025 (2 полюса) и < 0.001 (4 полюса), 4-полюсный notch на 1 kHz < 0.001 RMS. После правки параметров активный голос выдаёт те же сэмплы; у нового изменяется waveform. Питч C♯→C (C major) +12 st сравнен с C на октаву выше, максимальная допустимая погрешность `1e-6`.
- **Серийный стресс:** на 44.1/48/96 kHz цепь 4×Drive снова дала peak `0.278`, 0 full-scale samples, 0 dropped; пороги не ослаблены. Гранулярный тест отработал 54 комбинации Fs/block/voices с bypass и dual-filter, с glibc allocation probe, проверкой finite/peak/DC и 64 grains при 8 событиях. Например при 48 kHz × 128 × 8, dual-filter, локальные p50/p95/p99/max = `287.60 / 590.37 / 655.65 / 706.48 µs`. Это замеры движка в Linux sandbox (GCC 12.2, Xeon 2.60 GHz), не DAW deadline и не Windows.
- **Материалы для прослушивания:** 48 рендеров по трём частотам; повторные 48 kHz побайтно совпадают; 16 разных SHA-256. [TSV-манифест](evidence/review-catalog-962d5ef.tsv), архив WAV локально `build/fx-962d5ef/review-wavs.zip`, SHA-256 `fded710d3f4a48b2054646f351358db4ef49e2a030eeff56c0bbd9018c8ce9b4`. WAV не добавлены в Git; воспроизводимая сборка и SHA каждого файла в логе. Аудит проверяет `0.0001 < peak < 0.98`, `abs(DC) < 0.01`, отсутствие точных дубликатов и стабильные ID/роли; это не художественная приёмка.

## Каталог: кандидаты и утверждённые

| Роль | Новая ручная работа | Кандидаты / утверждены | Цель ТЗ |
|---|---|---:|---:|
| Impact | Subduction | 2 / 0 | 110 |
| Riser | Vernal Arc | 2 / 0 | 95 |
| Fall | Ash Tide | 2 / 0 | 65 |
| Sweep | Cedar Probe | 2 / 0 | 70 |
| Zap | Pixel Wasp | 2 / 0 | 105 |
| Rhythm | Sand Code | 2 / 0 | 75 |
| Atmosphere | Cloud Temple | 2 / 0 | 55 |
| Stinger | Copper Warning | 2 / 0 | 25 |
| **Итого** | | **16 / 0** | **600** |

## Внешние блокеры и следующий пакет

1. **P0 — среда native Linux.** Repro: strict CMake configure в `build/fx-native-suite` у pinned JUCE падает на `PKG_CONFIG_EXECUTABLE`. `sudo -n apt-get update/install` проверены; `deb.debian.org` недостижим из sandbox, в локальных индексах нет `pkg-config`, GUI/ALSA headers и Xvfb. Нужен provisioned Linux runner с этими пакетами. Worker executables на `juce_core`/`juce_cryptography` не заменяют `suite_fx_processor_tests` и GUI runtime.
2. **P0 — Windows exception evidence и квалификация.** Из исторических Actions `44172558893` / `44376476136` доступен `VOX-Suite-VST3`, но нет артефакта дампа/exception trace. Причину четырёх SEGFAULT не подтверждали. На Windows x64 изолировать падения CTest с exception code + stack/minidump; на интегрированном SHA выполнить CTest/Steinberg validator и записать p50/p95/p99/max с заранее заданными пределами. Устаревший CI не запускался.
3. **P1 — независимая музыкальная и финальная owner-приёмка.** Нет решения слушателя/TL и допуска к дистрибуции. Repro для прослушивания — сгенерировать каталог по команде из лога, сверить SHA; DAW-приёмка только после согласованного Windows-кандидата.

**Дальше:** первым запустить native тесты на подготовленной машине. Крупнейший оставшийся инженерный срез — полная типизированная матрица модуляции и редактируемые source-specific цели, затем её подключение к полноценному браузеру/генератору. Параллельно продолжить волны каталога 120→300→600 без подмены Seed-прототипами.
