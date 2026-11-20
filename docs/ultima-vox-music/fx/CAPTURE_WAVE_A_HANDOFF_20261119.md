# VOX FX — инженерная передача: restore, capture, Wave A

**Статус: интегрированный инженерный кандидат, не production-ready.** Ветка `arena/20b960e7-vox-electronic-engine`; последний проверенный снимок native/RT — `3427fb5022750d87a175a507049c153b62e7e3c2`. Исполняемый код продукта — `1babc0f1be8dc42c39f2d126a638e61c9996836b`; разница до `3427fb5` только в native test fixture, коммит этой передачи добавляет только документацию и логи. PR [#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156) оставлен Draft/HOLD/NO MERGE на `f042f955aee724636037471647a7c3be664e18bc`. Параметры, VST3 identity и workflows не изменены.

## Работа по пяти пакетам

1. **Native и restore.** Добавлен `processBlock`-тест с хостовым listener, который удерживает restore между публикацией структуры и выходом из уведомления параметра. Callback не принимает новый NoteOn из смешанного состояния; старый голос доживает до принятия новой публикации и уходит с fade. Регрессия проверяет restart, invalidation preview и non-realtime suppression. Заводской/пользовательский browser, stale Curve/Motion gestures, закрытый редактор, проектный round-trip и offscreen resize/scale исполнены с pinned JUCE при стеке 1 MiB.
2. **DSP и время.** Отдельный нативный `Tempo Delay (1/8)` в каждом из четырёх слотов: host BPM, fallback 120, точный интервал на 30–1000 BPM, ниже 30 ограничение в одну секунду. Amount — feedback, Wet — прежний host base, tap changes кроссфейдятся. Legacy free Delay остался с прежним поведением. Буферы по одному секунде на банк готовятся вне callback; жёсткий feedback tail cap 10 s сохранён. FXSL v3 и `.voxfx` v3 используются при наличии нового типа, читаются старые форматы; неправильно маркированный v1/v2 payload отвергается без публикации. Нормализация входа 64-route matrix выполняется раз на активный источник, порядок суммирования и старый звук не меняются. Stress теперь совмещает random MIDI с четырёхтактовым 1/32-triplet Pattern и ratchet×4.
3. **Workflow.** Async индекс User-файлов с ограничением 512 и проверкой формата; добавлены пересекаемые facets style/mood/use-case/duration/tonality наряду с role/mode/favorite, поиск по key и метки asset/duration. В User без баров честно показано `Host duration`, для шумовых программ key не выдуман. Управляемый .voxfx save/delete сохраняется, индивидуальный load/save всё ещё может ждать local filesystem. Устаревший preview render не может снова запустить мониторинг после restore/panic/prepare или non-realtime bounce. Декодер WAV совместно отменяет read/RIFF/PCM/SHA при supersession; смена selection не инвалидирует уже выданный lease. WAV capture теперь учитывает независимый переключатель Pattern, а не только `PlayMode::sequence`, и использует захваченный BPM для tempo delay. Обычная автоматизация DAW не перезаписывается структурной загрузкой.
4. **Каталог.** 24 вручную заданных композиции из 2–4 слоёв с отдельными family/Character/filter/timing/contour/FX и, для Sequence, расставленными нотами и ratchets. Например, *Crystal Bulkhead* — обратный шумовой разгон с ударом и granular tail; *Alloy Sunrise* — 16-тактовый тональный подъём; *Tokamak Ticks* — двухтактовая triplet-фраза; *Wire Wind* — нетональная loop. Всего 56 review-only, ни одного approved. Манифест v2 добавляет use case, длительность, key, assets и fingerprint структуры (без влияния audition pitch, seed, root или длительности); PCM fingerprints отдельные. Все 32 прежних WAV побайтно совпали.
5. **Квалификация.** Standalone, workers, нативный JUCE, ASan/UBSan/TSan, RT и рендеры исполнены. Единственный явный Windows `workflow_dispatch` с опубликованного проверенного code head получил HTTP 403 от GitHub App; Windows job не создан. PR body/opt-in и чужие ветки не изменялись. Full Linux CTest также не выполнен: установленный локально pkgconf позволил пройти прежний барьер, но теперь строгая сборка `juceaide` падает на `ft2build.h` и не находит полный Linux dev sysroot.

## Матрица FX-01…07 / FX-T01…20

**S** = `1babc0f1be8dc42c39f2d126a638e61c9996836b`, **N** = `3427fb5022750d87a175a507049c153b62e7e3c2`. Тестовые названия ниже относятся к [логу прогонов](evidence/capture-wave-a-qualification.txt). Выполненные Linux-тесты не означают закрытия релизного критерия.

| ID | Реализация и выполненная проверка | Остаток |
|---|---|---|
| FX-01 | N — executable processor/editor, restore и migration с прежними 19 ID | Windows wrapper/identity отдельно |
| FX-02 | S — 4 слоя, 20 curves и шесть режимов; N — callback parity и host-тесты | Реальные DAW transport/loop/automation |
| FX-03 | S — 12 семейных тестов, dual filters/key/Character; 56 review WAV | Спектр/слуховые golden и не все advanced family controls |
| FX-04 | S/N — typed 64 routes, 4 shared slots, tempo delay, Drive, tails | Не вся матрица alias/oversampling, формы step/ADSR остаются фиксированными |
| FX-05 | N browser/restore/capture; S workers, pattern, codecs/A-B/generator | Wave A/B/C, полная metadata/lock матрица, native OS drag |
| FX-06 | N workspace, telemetry, native offscreen resize и gestures | OS DPI, focus/keyboard/accessibility и часть advanced controls |
| FX-07 | S/N Linux-квалификация и review-аудио | Windows, approvals, DAW и installer отсутствуют |
| FX-T01 | S `FxSourceSoundTests` и семейные тесты | Не человеческий golden/listening |
| FX-T02 | S `FxLayerTests`, `FxProgramStoreTests`, `FxSoundGraphTests`; N restore publication | В DAW ещё не проверено |
| FX-T03 | S codec/curve trajectory; N native restore/undo/stale gesture | Полная матрица gestures и host automation каждой кривой |
| FX-T04 | S playback; N шесть режимов в finite audition и отдельный Pattern switch | Finite audition ≠ произвольная held/loop партия или tempo map |
| FX-T05 | S playback; N random MIDI/CC120/123/velocity-zero/panic; bounded retirements | Слуховые clicks и все host lifecycle сценарии |
| FX-T06 | S bar/timeline/transport; N synthetic clock и tempo-delay impulse | Переменные DAW tempo maps, реальный seek/stop/loop |
| FX-T07 | S source semantics; N тональные/шумовые данные в native browser | Внешнее музыкальное подтверждение |
| FX-T08 | S macro/modulation/sound graph; N 64-route stress | Полный Cartesian и формы step/ADSR |
| FX-T09 | S effects/autotests; N tempo delay + reverb + route parity; 4×Drive stress | Alias, fold-down и типы в хосте |
| FX-T10 | S `FxSampleDecodeTests`, `FxSampleControllerTests`, `FxSampleStoreTests`; TSan для controller/store | OS/dialog и project relink в DAW |
| FX-T11 | S generator/A-B/portable/library, N searchable native filter/save/restore | Полный набор lock-комбинаций и музыкальная генерация всех стилей |
| FX-T12 | S 56 отдельных Program hashes, 168 multi-rate renders и 56 точных повторов | 0 approved, все три волны незакрыты |
| FX-T13 | S scheduler/SMF, N последовательность в WAV и native ratchet burst | Editable transfer в DAW и OS drag |
| FX-T14 | S file/export; N 7 пар float32 с нулевой max delta | Не live bounce с произвольной входной записью/tempo automation |
| FX-T15 | N native controls и offscreen explicit scales 100/125/150/200% | Не Windows DPI/focus/реальные системные dialogs |
| FX-T16 | N in-flight listener, closed-editor restore, malformed rollback и v1/v2/v3 effects | Проектный reopen в DAW отсутствует |
| FX-T17 | Запрос N заблокирован HTTP 403, job не создан | CTest/validator/VST3 SHA и диагностика Windows crash |
| FX-T18 | N 54×160 блоков, probes; S 3 benchmark и санитайзеры | Изолированный Linux не квалифицирует Windows/DAW или нагрузку при конкуренции |
| FX-T19 | S immutable ID/revision и asset-free sources, manifest/PCM SHA | Право производства/дистрибуции всей stock collection не утверждено |
| FX-T20 | Не проводился | Финальный once-only Cubase 12–15/REAPER у владельца |

## Выполненные прогоны и воспроизведение

- **S standalone:** `gcc -O2 -c libs/vox-testkit/probe/AllocationProbeLinux.c -o probe.o`; `g++ -std=c++20 -O2 -pthread -DVOX_TEST_INTERPOSE_MALLOC=1 -Iproducts/fx -Ilibs/vox-preset -Ilibs/vox-testkit/probe tests/suite/$name.cpp libs/vox-preset/StateContainer.cpp probe.o -ldl -o "$name"`; `(ulimit -s 1024; ./"$name")`. 39 тестовых executable и 3 benchmark — exit 0; точный список в логе. JUCE-dependent cases вынесены отдельно.
- **N native:** [scoped build script](evidence/run-linux-native-fx.sh), GCC/G++ 12.2, JUCE `e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8`. Product objects `-O2`, JUCE modules `-O1`; последняя компиляция fixture + relink использовали те же неизменённые product objects. `FxProcessorTests` — 0 failures; `FxRuntimeStressTests` — 0 функциональных failures. Для WAV использовались 64/127/512/71/311 callback blocks при 48 kHz, synthetic 240 BPM, сравнивались оба канала целиком; parity **max abs error 0** во всех семи cases (порог `2e-6`), включая отдельный пост-эффектный хвост One Shot. Parity не обещается после произвольного host seek/automation.
- **S workers:** четыре линкованных теста `FxFileExportTests`, `FxSampleDecodeTests`, `FxSampleControllerTests`, `FxPresetLibraryTests` — 4/4; `-O1`, pinned JUCE core/cryptography, стек 1 MiB. **ASan+UBSan** `-O1 -g1 -fno-omit-frame-pointer -no-pie -fsanitize=address,undefined`, `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`: 11/11. **TSan** `-O1 -g1 -no-pie -fsanitize=thread`, `TSAN_OPTIONS=halt_on_error=1`: ProgramStore/SampleStore/SampleController/PresetLibrary 4/4. Уже собранные JUCE core/crypto вне instrumentation.
- **N timing:** Linux Xeon 2.60 GHz, два видимых CPU; 44.1/48/96 kHz × 64/128/512 × 1/4/8 стартовых голосов, два сценария (random MIDI и тот же stress + max ratchet), четыре granular слоя, до 64 grains, 64 routes, 4×Drive. Instrumented malloc и linked pthread lock probes: **0 / 0** в 54 ячейках; peak ≤0.105386, DC ≤0.0011591. На 48 kHz ×128 ×8 изолированный p50/p95/p99/max **274.76/380.93/387.88/392.14 µs** без Pattern и **312.42/409.66/442.14/452.81 µs** с max ratchet. Изолированно **0/54** max превышений физического `frames/rate`; на той же машине параллельно со сборкой — **50/54**. Deadline не является условием exit code этого теста и не считается релизным PASS.
- **Отрицательные результаты:** локальная проба с перемешанными объектами остановилась в `RangedAudioParameter::convertTo0to1`; повтор только из согласованных объектов прошёл. Это не объясняет исторические Windows SEGFAULT. Первая компиляция restore fixture отклонила `const unique_lock`, исправлено в N. Воспроизведение Linux blocker: `PATH="<dir-with-pkg-config>:$PATH" cmake -S . -B build/fx-strict -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON -DFETCHCONTENT_SOURCE_DIR_JUCE=<pinned-JUCE>` — exit 1 с реальным pkgconf 2.3.0: отсутствуют ALSA/GL/EGL/freetype2/fontconfig development metadata, первый fatal в `juceaide` — `ft2build.h`.

## Материалы для прослушивания

| Роль | Кандидаты / утверждено | Цель Wave C |
|---|---:|---:|
| Impact | 7 / 0 | 110 |
| Riser | 7 / 0 | 95 |
| Fall | 7 / 0 | 65 |
| Sweep | 7 / 0 | 70 |
| Zap | 7 / 0 | 105 |
| Rhythm | 7 / 0 | 75 |
| Atmosphere | 7 / 0 | 55 |
| Stinger | 7 / 0 | 25 |
| **Итого** | **56 / 0** | **600** |

Режимы: 24 One Shot, 9 Gate, 6 Continuous, 4 Loop, 7 Sequence, 6 Transition; 16 style labels. Из 168 рендеров peak `0.002078…0.051026`, max DC `0.0002907`, min mono/stereo RMS `0.90651`, max peak последних 5 ms `0.000163`. Последние две величины — измерения, а не утверждённые перцептивные пороги. Engine отклоняет non-finite/over-full-scale до упаковки WAV. Не нормализовали автоматически до одинакового пика и не объявляем громкость готовой для релиза. На waveform и structural duplicates выполнены независимые проверки; это не заменяет экспертный список слабых или слишком похожих звуков.

[Manifest](evidence/review-catalog-1babc0f.tsv) · [SHA-256](evidence/review-catalog-1babc0f.sha256) · [метрики](evidence/review-catalog-1babc0f-metrics.txt). Review archive `build/fx-1babc0f/review-56-wavs.zip`, 34 206 541 байт, SHA-256 `19d250263428d61b4fabcd35a77a7562c2b0bd22f2acb39805dd2bf31c1acfa6`, без Git-blob. Для воспроизведения: собрать `FxFactoryCandidatesTests` по команде S и запустить `./FxFactoryCandidatesTests OUT/catalog`; `(cd OUT/catalog && sha256sum -c /absolute/path/to/review-catalog-1babc0f.sha256)`. WAV exports — references для review, не factory bundle.

## Оставшиеся релизные блокеры

1. **P0 — Windows Actions write / exact-head run.** Ручной `gh workflow run suite.yml --ref arena/20b960e7-vox-electronic-engine` на `3427fb5` отклонён `403 Resource not accessible by integration` (workflow `411307353`). Нужен запуск с правом Actions write либо TL reconciliation и один `[RUN_FX_SUITE]` на интегрированном PR head. При повторе исторических четырёх SEGFAULT сохранить exceptions/stacks/minidumps: старые runs `44172558893`/`44376476136` их не содержат. Не помечать успешной проверку старого head.
2. **P1 — полноценный Linux/Windows display и dev sysroot.** Strict configure выше блокирует full Linux CTest. Xvfb отсутствует; загрузка release asset в песочницу прервалась на недоступном redirect host. Реальный DPI/focus/runtime dialogs проверить в целевой ОС; offscreen render вместо этого не засчитывается.
3. **P1 — human listening и разрешение распространения.** Ответственный фиксирует решение с `ID + revision + 48-kHz WAV hash`. Требуется отбор, а не автоматическое утверждение 56 рендеров.
4. **P1 — один финальный owner-only host цикл.** После интеграции и независимой проверки: Cubase 12–15/REAPER, install/uninstall, реальный timing/automation/restore и editable transfer с Drums/Synth в том же проекте. Не запускался.

**Следующий крупный незакрытый пакет:** дополнить Wave A ещё **64** отдельными кандидатами до 120, включая переходы/циклы с менее представленными стилями, и провести первичный слуховой отбор. Параллельно — довести полноту source-specific/step/ADSR редактирования, key/focus на реальных peers и Windows timing по согласованному бюджету; scope 300/600 остаётся в силе.
